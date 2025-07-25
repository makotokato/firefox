const { ProfileAge } = ChromeUtils.importESModule(
  "resource://gre/modules/ProfileAge.sys.mjs"
);

const gProfD = do_get_profile();
let ID = 0;

// Creates a unique profile directory to use for a test.
function withDummyProfile(task) {
  return async () => {
    let profile = PathUtils.join(gProfD.path, "" + ID++);
    await IOUtils.makeDirectory(profile);
    await task(profile);
    await IOUtils.remove(profile, { recursive: true });
  };
}

add_task(
  withDummyProfile(async profile => {
    let times = await ProfileAge(profile);
    Assert.greater(
      await times.created,
      0,
      "We can't really say what this will be, just assume if it is a number it's ok."
    );
    Assert.equal(
      await times.reset,
      undefined,
      "Reset time is undefined in a new profile"
    );
    Assert.lessOrEqual(
      await times.firstUse,
      Date.now(),
      "Should have initialised a first use time."
    );
  })
);

add_task(
  withDummyProfile(async profile => {
    const CREATED_TIME = Date.now() - 2000;
    const RESET_TIME = Date.now() - 1000;
    const RECOVERY_TIME = Date.now() - 500;

    await IOUtils.writeJSON(PathUtils.join(profile, "times.json"), {
      created: CREATED_TIME,
    });

    let times = await ProfileAge(profile);
    Assert.equal(
      await times.created,
      CREATED_TIME,
      "Should have seen the right profile time."
    );
    Assert.equal(
      await times.firstUse,
      undefined,
      "Should be no first use time."
    );

    let times2 = await ProfileAge(profile);
    Assert.equal(times, times2, "Should have got the same instance.");

    let promise = times.recordProfileReset(RESET_TIME);
    Assert.equal(
      await times2.reset,
      RESET_TIME,
      "Should have seen the right reset time in the second instance immediately."
    );
    await promise;

    let recoveryPromise = times.recordRecoveredFromBackup(RECOVERY_TIME);
    Assert.equal(
      await times2.recoveredFromBackup,
      RECOVERY_TIME,
      "Should have seen the right backup recovery time in the second instance immediately."
    );
    await recoveryPromise;

    let results = await IOUtils.readJSON(PathUtils.join(profile, "times.json"));
    Assert.deepEqual(
      results,
      {
        created: CREATED_TIME,
        reset: RESET_TIME,
        recoveredFromBackup: RECOVERY_TIME,
      },
      "Should have seen the right results."
    );
  })
);

add_task(
  withDummyProfile(async profile => {
    const RESET_TIME = Date.now() - 1000;
    const RESET_TIME2 = Date.now() - 2000;

    // The last call to recordProfileReset should always win.
    let times = await ProfileAge(profile);
    await Promise.all([
      times.recordProfileReset(RESET_TIME),
      times.recordProfileReset(RESET_TIME2),
    ]);

    let results = await IOUtils.readJSON(PathUtils.join(profile, "times.json"));
    delete results.firstUse;
    Assert.deepEqual(
      results,
      {
        reset: RESET_TIME2,
      },
      "Should have seen the right results."
    );
  })
);

add_task(
  withDummyProfile(async profile => {
    const CREATED_TIME = Date.now() - 1000;

    await IOUtils.writeJSON(PathUtils.join(profile, "times.json"), {
      created: CREATED_TIME,
      firstUse: null,
    });

    let times = await ProfileAge(profile);
    Assert.lessOrEqual(
      await times.firstUse,
      Date.now(),
      "Should have initialised a first use time."
    );
  })
);

add_task(
  withDummyProfile(async profile => {
    const RECOVERY_TIME = Date.now() - 1000;
    const RECOVERY_TIME2 = Date.now() - 2000;

    // The last call to recordRecoveredFromBackup should always win.
    let times = await ProfileAge(profile);
    await Promise.all([
      times.recordRecoveredFromBackup(RECOVERY_TIME),
      times.recordRecoveredFromBackup(RECOVERY_TIME2),
    ]);

    let results = await IOUtils.readJSON(PathUtils.join(profile, "times.json"));
    Assert.equal(
      results.recoveredFromBackup,
      RECOVERY_TIME2,
      "Should have seen the right results."
    );
  })
);
