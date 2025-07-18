/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef NSFILEPICKERPROXY_H
#define NSFILEPICKERPROXY_H

#include "nsBaseFilePicker.h"
#include "nsString.h"
#include "nsIURI.h"
#include "nsTArray.h"
#include "nsCOMArray.h"

#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/PFilePickerChild.h"

class nsIWidget;
class nsIFile;
class nsPIDOMWindowInner;

namespace mozilla::dom {

class OwningFileOrDirectory;

}  // namespace mozilla::dom

/**
  This class creates a proxy file picker to be used in content processes.
  The file picker just collects the initialization data and when Open() is
  called, remotes everything to the chrome process which in turn can show a
  platform specific file picker.
*/
class nsFilePickerProxy : public nsBaseFilePicker,
                          public mozilla::dom::PFilePickerChild {
 public:
  nsFilePickerProxy();

  NS_DECL_ISUPPORTS

  // nsIFilePicker (less what's in nsBaseFilePicker)
  NS_IMETHOD Init(mozilla::dom::BrowsingContext* aBrowsingContext,
                  const nsAString& aTitle, nsIFilePicker::Mode aMode) override;
  NS_IMETHOD AppendFilter(const nsAString& aTitle,
                          const nsAString& aFilter) override;
  NS_IMETHOD GetCapture(nsIFilePicker::CaptureTarget* aCapture) override;
  NS_IMETHOD SetCapture(nsIFilePicker::CaptureTarget aCapture) override;
  NS_IMETHOD GetDefaultString(nsAString& aDefaultString) override;
  NS_IMETHOD SetDefaultString(const nsAString& aDefaultString) override;
  NS_IMETHOD GetDefaultExtension(nsAString& aDefaultExtension) override;
  NS_IMETHOD SetDefaultExtension(const nsAString& aDefaultExtension) override;
  NS_IMETHOD GetFilterIndex(int32_t* aFilterIndex) override;
  NS_IMETHOD SetFilterIndex(int32_t aFilterIndex) override;
  NS_IMETHOD GetFile(nsIFile** aFile) override;
  NS_IMETHOD GetFileURL(nsIURI** aFileURL) override;
  NS_IMETHOD GetFiles(nsISimpleEnumerator** aFiles) override;

  NS_IMETHOD GetDomFileOrDirectory(nsISupports** aValue) override;
  NS_IMETHOD GetDomFileOrDirectoryEnumerator(
      nsISimpleEnumerator** aValue) override;
  NS_IMETHOD GetDomFilesInWebKitDirectory(
      nsISimpleEnumerator** aValue) override;

  NS_IMETHOD Open(nsIFilePickerShownCallback* aCallback) override;

  // PFilePickerChild
  virtual mozilla::ipc::IPCResult Recv__delete__(
      const MaybeInputData& aData,
      const nsIFilePicker::ResultCode& aResult) override;

 private:
  ~nsFilePickerProxy();
  void InitNative(nsIWidget*, const nsAString&) override;
  nsresult ResolveSpecialDirectory(const nsAString& aSpecialDirectory) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  nsTArray<mozilla::dom::OwningFileOrDirectory> mFilesOrDirectories;
  nsCOMPtr<nsIFilePickerShownCallback> mCallback;
  nsTArray<mozilla::dom::OwningFileOrDirectory> mFilesInWebKitDirectory;

  int16_t mSelectedType;
  nsString mFile;
  nsString mDefault;
  nsString mDefaultExtension;
  nsIFilePicker::CaptureTarget mCapture;

  bool mIPCActive;

  nsTArray<nsString> mFilters;
  nsTArray<nsString> mFilterNames;
};

#endif  // NSFILEPICKERPROXY_H
