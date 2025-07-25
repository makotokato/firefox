/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MultiLogCTVerifier.h"

#include <stdint.h>

#include "CTLogVerifier.h"
#include "CTObjectsExtractor.h"
#include "CTSerialization.h"
#include "CTTestUtils.h"
#include "gtest/gtest.h"
#include "nss.h"

namespace mozilla {
namespace ct {

using namespace mozilla::pkix;

class MultiLogCTVerifierTest : public ::testing::Test {
 public:
  MultiLogCTVerifierTest() : mNow(Time::uninitialized), mLogOperatorID(123) {}

  void SetUp() override {
    // Does nothing if NSS is already initialized.
    if (NSS_NoDB_Init(nullptr) != SECSuccess) {
      abort();
    }

    CTLogVerifier log(mLogOperatorID, CTLogState::Admissible,
                      CTLogFormat::RFC6962, 0);
    ;
    ASSERT_EQ(Success, log.Init(InputForBuffer(GetTestPublicKey())));
    mVerifier.AddLog(std::move(log));

    mTestCert = GetDEREncodedX509Cert();
    mEmbeddedCert = GetDEREncodedTestEmbeddedCert();
    mCaCert = GetDEREncodedCACert();
    mCaCertSPKI = ExtractCertSPKI(mCaCert);
    mIntermediateCert = GetDEREncodedIntermediateCert();
    mIntermediateCertSPKI = ExtractCertSPKI(mIntermediateCert);

    // Set the current time making sure all test timestamps are in the past.
    mNow =
        TimeFromEpochInSeconds(1451606400u);  // Date.parse("2016-01-01")/1000
  }

  void CheckForSingleValidSCTInResult(const CTVerifyResult& result,
                                      SCTOrigin origin) {
    EXPECT_EQ(0U, result.decodingErrors);
    ASSERT_EQ(1U, result.verifiedScts.size());
    EXPECT_EQ(CTLogState::Admissible, result.verifiedScts[0].logState);
    EXPECT_EQ(origin, result.verifiedScts[0].origin);
    EXPECT_EQ(mLogOperatorID, result.verifiedScts[0].logOperatorId);
  }

  // Writes an SCTList containing a single |sct| into |output|.
  void EncodeSCTListForTesting(Input sct, Buffer& output) {
    std::vector<Input> list;
    list.push_back(std::move(sct));
    ASSERT_EQ(Success, EncodeSCTList(list, output));
  }

  void GetSCTListWithInvalidLogID(Buffer& result) {
    result.clear();
    Buffer sct(GetTestSignedCertificateTimestamp());
    // Change a byte inside the Log ID part of the SCT so it does
    // not match the log used in the tests.
    sct[15] ^= '\xFF';
    EncodeSCTListForTesting(InputForBuffer(sct), result);
  }

  void CheckPrecertVerification(const Buffer& cert, const Buffer& issuerSPKI) {
    Buffer sctList;
    ExtractEmbeddedSCTList(cert, sctList);
    ASSERT_FALSE(sctList.empty());

    CTVerifyResult result;
    ASSERT_EQ(Success,
              mVerifier.Verify(InputForBuffer(cert), InputForBuffer(issuerSPKI),
                               InputForBuffer(sctList), Input(), Input(), mNow,
                               Nothing(), result));
    CheckForSingleValidSCTInResult(result, SCTOrigin::Embedded);
  }

 protected:
  MultiLogCTVerifier mVerifier;
  Buffer mTestCert;
  Buffer mEmbeddedCert;
  Buffer mCaCert;
  Buffer mCaCertSPKI;
  Buffer mIntermediateCert;
  Buffer mIntermediateCertSPKI;
  Time mNow;
  CTLogOperatorId mLogOperatorID;
};

// Test that an embedded SCT can be extracted and the extracted SCT contains
// the expected data. This tests the ExtractEmbeddedSCTList function from
// CTTestUtils.h that other tests here rely upon.
TEST_F(MultiLogCTVerifierTest, ExtractEmbeddedSCT) {
  SignedCertificateTimestamp sct;

  // Extract the embedded SCT.

  Buffer sctList;
  ExtractEmbeddedSCTList(mEmbeddedCert, sctList);
  ASSERT_FALSE(sctList.empty());

  Reader sctReader;
  ASSERT_EQ(Success, DecodeSCTList(InputForBuffer(sctList), sctReader));
  Input sctItemInput;
  ASSERT_EQ(Success, ReadSCTListItem(sctReader, sctItemInput));
  EXPECT_TRUE(sctReader.AtEnd());  // we only expect one sct in the list

  Reader sctItemReader(sctItemInput);
  ASSERT_EQ(Success, DecodeSignedCertificateTimestamp(sctItemReader, sct));

  // Make sure the SCT contains the expected data.

  EXPECT_EQ(SignedCertificateTimestamp::Version::V1, sct.version);
  EXPECT_EQ(GetTestPublicKeyId(), sct.logId);

  uint64_t expectedTimestamp = 1365181456275;
  EXPECT_EQ(expectedTimestamp, sct.timestamp);
}

TEST_F(MultiLogCTVerifierTest, VerifiesEmbeddedSCT) {
  CheckPrecertVerification(mEmbeddedCert, mCaCertSPKI);
}

TEST_F(MultiLogCTVerifierTest, VerifiesEmbeddedSCTWithPreCA) {
  CheckPrecertVerification(GetDEREncodedTestEmbeddedWithPreCACert(),
                           mCaCertSPKI);
}

TEST_F(MultiLogCTVerifierTest, VerifiesEmbeddedSCTWithIntermediate) {
  CheckPrecertVerification(GetDEREncodedTestEmbeddedWithIntermediateCert(),
                           mIntermediateCertSPKI);
}

TEST_F(MultiLogCTVerifierTest, VerifiesEmbeddedSCTWithIntermediateAndPreCA) {
  CheckPrecertVerification(GetDEREncodedTestEmbeddedWithIntermediatePreCACert(),
                           mIntermediateCertSPKI);
}

TEST_F(MultiLogCTVerifierTest, VerifiesSCTFromOCSP) {
  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success, mVerifier.Verify(InputForBuffer(mTestCert), Input(),
                                      Input(), InputForBuffer(sctList), Input(),
                                      mNow, Nothing(), result));

  CheckForSingleValidSCTInResult(result, SCTOrigin::OCSPResponse);
}

TEST_F(MultiLogCTVerifierTest, VerifiesSCTFromTLS) {
  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success, mVerifier.Verify(InputForBuffer(mTestCert), Input(),
                                      Input(), Input(), InputForBuffer(sctList),
                                      mNow, Nothing(), result));

  CheckForSingleValidSCTInResult(result, SCTOrigin::TLSExtension);
}

TEST_F(MultiLogCTVerifierTest, VerifiesSCTFromMultipleSources) {
  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success,
            mVerifier.Verify(InputForBuffer(mTestCert), Input(), Input(),
                             InputForBuffer(sctList), InputForBuffer(sctList),
                             mNow, Nothing(), result));

  // The result should contain verified SCTs from TLS and OCSP origins.
  size_t embeddedCount = 0;
  size_t tlsExtensionCount = 0;
  size_t ocspResponseCount = 0;
  for (const VerifiedSCT& verifiedSct : result.verifiedScts) {
    EXPECT_EQ(CTLogState::Admissible, verifiedSct.logState);
    switch (verifiedSct.origin) {
      case SCTOrigin::Embedded:
        embeddedCount++;
        break;
      case SCTOrigin::TLSExtension:
        tlsExtensionCount++;
        break;
      case SCTOrigin::OCSPResponse:
        ocspResponseCount++;
        break;
    }
  }
  EXPECT_EQ(embeddedCount, 0u);
  EXPECT_TRUE(tlsExtensionCount > 0);
  EXPECT_TRUE(ocspResponseCount > 0);
}

TEST_F(MultiLogCTVerifierTest, IdentifiesSCTFromUnknownLog) {
  Buffer sctList;
  GetSCTListWithInvalidLogID(sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success, mVerifier.Verify(InputForBuffer(mTestCert), Input(),
                                      Input(), Input(), InputForBuffer(sctList),
                                      mNow, Nothing(), result));

  EXPECT_EQ(0U, result.decodingErrors);
  EXPECT_EQ(0U, result.verifiedScts.size());
  EXPECT_EQ(1U, result.sctsFromUnknownLogs);
}

TEST_F(MultiLogCTVerifierTest, IdentifiesSCTFromDisqualifiedLog) {
  MultiLogCTVerifier verifier;
  const uint64_t retiredTime = 12345u;
  CTLogVerifier log(mLogOperatorID, CTLogState::Retired, CTLogFormat::RFC6962,
                    retiredTime);
  ASSERT_EQ(Success, log.Init(InputForBuffer(GetTestPublicKey())));
  verifier.AddLog(std::move(log));

  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success, verifier.Verify(InputForBuffer(mTestCert), Input(),
                                     Input(), Input(), InputForBuffer(sctList),
                                     mNow, Nothing(), result));

  EXPECT_EQ(0U, result.decodingErrors);
  ASSERT_EQ(1U, result.verifiedScts.size());
  EXPECT_EQ(CTLogState::Retired, result.verifiedScts[0].logState);
  EXPECT_EQ(retiredTime, result.verifiedScts[0].logTimestamp);
  EXPECT_EQ(mLogOperatorID, result.verifiedScts[0].logOperatorId);
}

TEST_F(MultiLogCTVerifierTest, VerifiesSCTFromBeforeDistrust) {
  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success, mVerifier.Verify(InputForBuffer(mTestCert), Input(),
                                      Input(), Input(), InputForBuffer(sctList),
                                      mNow, Some(mNow), result));

  EXPECT_EQ(0U, result.decodingErrors);
  EXPECT_EQ(1U, result.verifiedScts.size());
  EXPECT_EQ(0U, result.sctsWithDistrustedTimestamps);
}

TEST_F(MultiLogCTVerifierTest, IdentifiesSCTFromAfterDistrust) {
  Buffer sct(GetTestSignedCertificateTimestamp());
  Buffer sctList;
  EncodeSCTListForTesting(InputForBuffer(sct), sctList);

  CTVerifyResult result;
  ASSERT_EQ(Success,
            mVerifier.Verify(InputForBuffer(mTestCert), Input(), Input(),
                             Input(), InputForBuffer(sctList), mNow,
                             Some(TimeFromEpochInSeconds(100)), result));

  EXPECT_EQ(0U, result.decodingErrors);
  EXPECT_EQ(0U, result.verifiedScts.size());
  EXPECT_EQ(1U, result.sctsWithDistrustedTimestamps);
}

}  // namespace ct
}  // namespace mozilla
