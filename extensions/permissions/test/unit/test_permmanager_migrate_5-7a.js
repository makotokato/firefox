/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
});

var PERMISSIONS_FILE_NAME = "permissions.sqlite";

function GetPermissionsFile(profile) {
  let file = profile.clone();
  file.append(PERMISSIONS_FILE_NAME);
  return file;
}

add_task(async function test() {
  // Create and set up the permissions database.
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  let profile = do_get_profile();

  // We need to execute a pm method to be sure that the DB is fully
  // initialized.
  var pm = Services.perms;
  Assert.equal(pm.all.length, 0, "No cookies");

  let db = Services.storage.openDatabase(GetPermissionsFile(profile));
  db.schemaVersion = 5;
  db.executeSimpleSQL("DROP TABLE moz_perms");
  db.executeSimpleSQL("DROP TABLE moz_hosts");

  /*
   * V5 table
   */
  db.executeSimpleSQL(
    "CREATE TABLE moz_hosts (" +
      " id INTEGER PRIMARY KEY" +
      ",origin TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",modificationTime INTEGER" +
      ")"
  );

  let stmt5Insert = db.createStatement(
    "INSERT INTO moz_hosts (" +
      "id, origin, type, permission, expireType, expireTime, modificationTime" +
      ") VALUES (" +
      ":id, :origin, :type, :permission, :expireType, :expireTime, :modificationTime" +
      ")"
  );

  /*
   * V4 table
   */
  db.executeSimpleSQL(
    "CREATE TABLE moz_hosts_v4 (" +
      " id INTEGER PRIMARY KEY" +
      ",host TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",modificationTime INTEGER" +
      ",appId INTEGER" +
      ",isInBrowserElement INTEGER" +
      ")"
  );

  let stmtInsert = db.createStatement(
    "INSERT INTO moz_hosts_v4 (" +
      "id, host, type, permission, expireType, expireTime, modificationTime, appId, isInBrowserElement" +
      ") VALUES (" +
      ":id, :host, :type, :permission, :expireType, :expireTime, :modificationTime, :appId, :isInBrowserElement" +
      ")"
  );

  let id = 0;

  function insertOrigin(
    origin,
    type,
    permission,
    expireType,
    expireTime,
    modificationTime
  ) {
    let thisId = id++;

    stmt5Insert.bindByName("id", thisId);
    stmt5Insert.bindByName("origin", origin);
    stmt5Insert.bindByName("type", type);
    stmt5Insert.bindByName("permission", permission);
    stmt5Insert.bindByName("expireType", expireType);
    stmt5Insert.bindByName("expireTime", expireTime);
    stmt5Insert.bindByName("modificationTime", modificationTime);

    try {
      stmt5Insert.execute();
    } finally {
      stmt5Insert.reset();
    }

    return {
      id: thisId,
      origin,
      type,
      permission,
      expireType,
      expireTime,
      modificationTime,
    };
  }
  function insertHost(
    host,
    type,
    permission,
    expireType,
    expireTime,
    modificationTime,
    appId,
    isInBrowserElement
  ) {
    let thisId = id++;

    stmtInsert.bindByName("id", thisId);
    stmtInsert.bindByName("host", host);
    stmtInsert.bindByName("type", type);
    stmtInsert.bindByName("permission", permission);
    stmtInsert.bindByName("expireType", expireType);
    stmtInsert.bindByName("expireTime", expireTime);
    stmtInsert.bindByName("modificationTime", modificationTime);
    stmtInsert.bindByName("appId", appId);
    stmtInsert.bindByName("isInBrowserElement", isInBrowserElement);

    try {
      stmtInsert.execute();
    } finally {
      stmtInsert.reset();
    }

    return {
      id: thisId,
      host,
      type,
      permission,
      expireType,
      expireTime,
      modificationTime,
      appId,
      isInBrowserElement,
    };
  }

  let created5 = [
    insertOrigin("https://foo.com", "A", 2, 0, 0, 0),
    insertOrigin("http://foo.com", "A", 2, 0, 0, 0),
    insertOrigin("http://foo.com^inBrowser=1", "A", 2, 0, 0, 0),
  ];

  // Add some rows to the database
  // eslint-disable-next-line no-unused-vars
  let created = [
    insertHost("foo.com", "A", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "C", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 1000, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 2000, true),
    insertHost("sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("subber.sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 1000, false),
    insertHost("bar.ca", "A", 1, 0, 0, 0, 1000, true),
    insertHost("localhost", "A", 1, 0, 0, 0, 0, false),
    insertHost("127.0.0.1", "A", 1, 0, 0, 0, 0, false),
    insertHost("192.0.2.235", "A", 1, 0, 0, 0, 0, false),
    insertHost("file:///some/path/to/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost("file:///another/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost(
      "moz-nullprincipal:{8695105a-adbe-4e4e-8083-851faa5ca2d7}",
      "A",
      1,
      0,
      0,
      0,
      0,
      false
    ),
    insertHost(
      "moz-nullprincipal:{12ahjksd-akjs-asd3-8393-asdu2189asdu}",
      "B",
      1,
      0,
      0,
      0,
      0,
      false
    ),
    insertHost("<file>", "A", 1, 0, 0, 0, 0, false),
    insertHost("<file>", "B", 1, 0, 0, 0, 0, false),
  ];

  // CLose the db connection
  stmt5Insert.finalize();
  stmtInsert.finalize();
  db.close();
  stmtInsert = null;
  db = null;

  let expected = [
    // The http:// entries under foo.com won't be inserted, as there are history entries for foo.com,
    // and http://foo.com or a subdomain are never visited.
    // ["http://foo.com", "A", 1, 0, 0],
    //
    // Because we search for port/scheme combinations under eTLD+1, we should not have http:// entries
    // for subdomains of foo.com either
    // ["http://sub.foo.com", "B", 1, 0, 0],
    // ["http://subber.sub.foo.com", "B", 1, 0, 0],

    ["https://foo.com", "A", 1, 0, 0],
    ["https://foo.com", "C", 1, 0, 0],
    ["https://sub.foo.com", "B", 1, 0, 0],
    ["https://subber.sub.foo.com", "B", 1, 0, 0],

    // bar.ca will have both http:// and https:// for all entries, because there are no associated history entries
    ["http://bar.ca", "B", 1, 0, 0],
    ["https://bar.ca", "B", 1, 0, 0],
    ["http://bar.ca", "A", 1, 0, 0],
    ["https://bar.ca", "A", 1, 0, 0],
    ["file:///some/path/to/file.html", "A", 1, 0, 0],
    ["file:///another/file.html", "A", 1, 0, 0],

    // Because we put ftp://some.subdomain.of.foo.com:8000/some/subdirectory in the history, we should
    // also have these entries
    ["ftp://foo.com:8000", "A", 1, 0, 0],
    ["ftp://foo.com:8000", "C", 1, 0, 0],

    // In addition, because we search for port/scheme combinations under eTLD+1, we should have the
    // following entries
    ["ftp://sub.foo.com:8000", "B", 1, 0, 0],
    ["ftp://subber.sub.foo.com:8000", "B", 1, 0, 0],

    // Make sure that we also support localhost, and IP addresses
    ["http://localhost", "A", 1, 0, 0],
    ["https://localhost", "A", 1, 0, 0],
    ["http://127.0.0.1", "A", 1, 0, 0],
    ["https://127.0.0.1", "A", 1, 0, 0],
    ["http://192.0.2.235", "A", 1, 0, 0],
    ["https://192.0.2.235", "A", 1, 0, 0],
  ];

  let found = expected.map(() => 0);

  // Add some places to the places database
  await PlacesTestUtils.addVisits(
    Services.io.newURI("https://foo.com/some/other/subdirectory")
  );
  await PlacesTestUtils.addVisits(
    Services.io.newURI("ftp://some.subdomain.of.foo.com:8000/some/subdirectory")
  );

  // This will force the permission-manager to reload the data.
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  // Force initialization of the PermissionManager
  for (let permission of Services.perms.all) {
    let isExpected = false;

    expected.forEach((it, i) => {
      if (
        permission.principal.origin == it[0] &&
        permission.type == it[1] &&
        permission.capability == it[2] &&
        permission.expireType == it[3] &&
        permission.expireTime == it[4]
      ) {
        isExpected = true;
        found[i]++;
      }
    });

    Assert.ok(
      isExpected,
      "Permission " +
        (isExpected ? "should" : "shouldn't") +
        " be in permission database: " +
        permission.principal.origin +
        ", " +
        permission.type +
        ", " +
        permission.capability +
        ", " +
        permission.expireType +
        ", " +
        permission.expireTime
    );
  }

  found.forEach((count, i) => {
    Assert.equal(
      count,
      1,
      "Expected count = 1, got count = " +
        count +
        " for permission " +
        expected[i]
    );
  });

  // Check to make sure that all of the tables which we care about are present
  {
    db = Services.storage.openDatabase(GetPermissionsFile(profile));
    Assert.ok(db.tableExists("moz_perms"));
    Assert.ok(db.tableExists("moz_hosts"));
    Assert.ok(!db.tableExists("moz_hosts_is_backup"));
    Assert.ok(db.tableExists("moz_perms_v6"));

    // The moz_hosts table should still exist but be empty
    let mozHostsCount = db.createStatement("SELECT count(*) FROM moz_hosts");
    try {
      mozHostsCount.executeStep();
      Assert.equal(mozHostsCount.getInt64(0), 0);
    } finally {
      mozHostsCount.finalize();
    }

    // Check that the moz_perms_v6 table contains the backup of the entry we created
    let mozPermsV6Stmt = db.createStatement(
      "SELECT " +
        "origin, type, permission, expireType, expireTime, modificationTime " +
        "FROM moz_perms_v6 WHERE id = :id"
    );
    try {
      // Check that the moz_hosts table still contains the correct values.
      created5.forEach(it => {
        mozPermsV6Stmt.reset();
        mozPermsV6Stmt.bindByName("id", it.id);
        mozPermsV6Stmt.executeStep();
        Assert.equal(mozPermsV6Stmt.getUTF8String(0), it.origin);
        Assert.equal(mozPermsV6Stmt.getUTF8String(1), it.type);
        Assert.equal(mozPermsV6Stmt.getInt64(2), it.permission);
        Assert.equal(mozPermsV6Stmt.getInt64(3), it.expireType);
        Assert.equal(mozPermsV6Stmt.getInt64(4), it.expireTime);
        Assert.equal(mozPermsV6Stmt.getInt64(5), it.modificationTime);
      });
    } finally {
      mozPermsV6Stmt.finalize();
    }

    // Check that there are the right number of values
    let mozPermsV6Count = db.createStatement(
      "SELECT count(*) FROM moz_perms_v6"
    );
    try {
      mozPermsV6Count.executeStep();
      Assert.equal(mozPermsV6Count.getInt64(0), created5.length);
    } finally {
      mozPermsV6Count.finalize();
    }

    db.close();
  }
});
