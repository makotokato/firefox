/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  _ExperimentFeature: "resource://nimbus/ExperimentAPI.sys.mjs",
  ASRouterTargeting:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  TargetingContext: "resource://messaging-system/targeting/Targeting.sys.mjs",
  recordTargetingContext:
    "resource://nimbus/lib/TargetingContextRecorder.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("RSLoader");
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  "nsIUpdateTimerManager"
);

const COLLECTION_ID_PREF = "messaging-system.rsexperimentloader.collection_id";
const COLLECTION_ID_FALLBACK = "nimbus-desktop-experiments";
const TARGETING_CONTEXT_TELEMETRY_ENABLED_PREF =
  "nimbus.telemetry.targetingContextEnabled";

const TIMER_NAME = "rs-experiment-loader-timer";
const TIMER_LAST_UPDATE_PREF = `app.update.lastUpdateTime.${TIMER_NAME}`;
// Use the same update interval as normandy
const RUN_INTERVAL_PREF = "app.normandy.run_interval_seconds";
const NIMBUS_DEBUG_PREF = "nimbus.debug";
const NIMBUS_VALIDATION_PREF = "nimbus.validation.enabled";
const NIMBUS_APPID_PREF = "nimbus.appId";

const SECURE_EXPERIMENTS_COLLECTION_ID = "nimbus-secure-experiments";

const EXPERIMENTS_COLLECTION = "experiments";
const SECURE_EXPERIMENTS_COLLECTION = "secureExperiments";

const IS_MAIN_PROCESS =
  Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

const SECURE_FEATURE_IDS = new Set(["prefFlips", "newtabTrainhopAddon"]);
const RS_COLLECTION_OPTIONS = {
  [EXPERIMENTS_COLLECTION]: {
    // None of these features can be present to accept an experiment from the
    // experiments collection.
    disallowedFeatureIds: SECURE_FEATURE_IDS,
  },

  [SECURE_EXPERIMENTS_COLLECTION]: {
    // One of these features *must* be present to accept an experiment from the
    // secure experiments collection.
    requiredFeatureIds: SECURE_FEATURE_IDS,
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "COLLECTION_ID",
  COLLECTION_ID_PREF,
  COLLECTION_ID_FALLBACK
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "NIMBUS_DEBUG",
  NIMBUS_DEBUG_PREF,
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "APP_ID",
  NIMBUS_APPID_PREF,
  "firefox-desktop"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "TARGETING_CONTEXT_TELEMETRY_ENABLED",
  TARGETING_CONTEXT_TELEMETRY_ENABLED_PREF
);

const SCHEMAS = {
  get NimbusExperiment() {
    return fetch("resource://nimbus/schemas/NimbusExperiment.schema.json", {
      credentials: "omit",
    }).then(rsp => rsp.json());
  },
};

export const MatchStatus = Object.freeze({
  ENROLLMENT_PAUSED: "ENROLLMENT_PAUSED",
  NOT_SEEN: "NOT_SEEN",
  NO_MATCH: "NO_MATCH",
  TARGETING_ONLY: "TARGETING_ONLY",
  TARGETING_AND_BUCKETING: "TARGETING_AND_BUCKETING",
  UNENROLLED_IN_ANOTHER_PROFILE: "UNENROLLED_IN_ANOTHER_PROFILE",
});

export const CheckRecipeResult = {
  Ok(status) {
    return {
      ok: true,
      status,
    };
  },

  InvalidRecipe() {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.INVALID_RECIPE,
    };
  },

  InvalidBranches(branchSlugs) {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.INVALID_BRANCH,
      branchSlugs,
    };
  },

  InvalidFeatures(featureIds) {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.INVALID_FEATURE,
      featureIds,
    };
  },

  MissingL10nEntry(locale, missingL10nIds) {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_ENTRY,
      locale,
      missingL10nIds,
    };
  },

  MissingLocale(locale) {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_LOCALE,
      locale,
    };
  },

  UnsupportedFeatures() {
    return {
      ok: false,
      reason: lazy.NimbusTelemetry.ValidationFailureReason.UNSUPPORTED_FEATURES,
    };
  },
};

export class RemoteSettingsExperimentLoader {
  /**
   * A shutdown blocker that will try to ensure that any ongoing update will
   * finish.
   *
   * @type {function(): Promise<void>}
   */
  #shutdownBlocker;

  get LOCK_ID() {
    return "remote-settings-experiment-loader:update";
  }

  get SOURCE() {
    return lazy.NimbusTelemetry.EnrollmentSource.RS_LOADER;
  }

  constructor(manager) {
    this.manager = manager;

    // Has the timer been set?
    this._enabled = false;
    // Are we in the middle of updating recipes already?
    this._updating = false;
    // Have we updated recipes at least once?
    this._hasUpdatedOnce = false;
    // deferred promise object that resolves after recipes are updated
    this._updatingDeferred = Promise.withResolvers();

    this.remoteSettingsClients = {};
    ChromeUtils.defineLazyGetter(
      this.remoteSettingsClients,
      EXPERIMENTS_COLLECTION,
      () => {
        return lazy.RemoteSettings(lazy.COLLECTION_ID);
      }
    );
    ChromeUtils.defineLazyGetter(
      this.remoteSettingsClients,
      SECURE_EXPERIMENTS_COLLECTION,
      () => {
        return lazy.RemoteSettings(SECURE_EXPERIMENTS_COLLECTION_ID);
      }
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "intervalInSeconds",
      RUN_INTERVAL_PREF,
      21600,
      () => this.setTimer()
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "validationEnabled",
      NIMBUS_VALIDATION_PREF,
      true
    );
  }

  /**
   * Initialize the loader, updating recipes from Remote Settings.
   *
   * @param {Object} options            additional options.
   * @param {bool}   options.forceSync  force Remote Settings to sync recipe collection
   *                                    before updating recipes; throw if sync fails.
   * @return {Promise}                  which resolves after initialization and recipes
   *                                    are updated.
   */
  async enable({ forceSync = false } = {}) {
    if (!IS_MAIN_PROCESS) {
      throw new Error(
        "RemoteSettingsExperimentLoader.enable() can only be called from the main process"
      );
    }

    if (this._enabled) {
      return;
    }

    if (!lazy.ExperimentAPI.studiesEnabled) {
      lazy.log.debug(
        "Not enabling RemoteSettingsExperimentLoader: studies disabled"
      );
      return;
    }

    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      lazy.log.debug(
        "Not enabling RemoteSettingsExperimentLoader: shutting down"
      );
      return;
    }

    this.#shutdownBlocker = async () => {
      await this.finishedUpdating();
      this.disable();
    };

    lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
      "RemoteSettingsExperimentLoader: disabling",
      this.#shutdownBlocker
    );

    this.setTimer();
    this._enabled = true;

    await this.updateRecipes("enabled", { forceSync });
  }

  disable() {
    if (!this._enabled) {
      return;
    }

    lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(
      this.#shutdownBlocker
    );
    this.#shutdownBlocker = null;

    lazy.timerManager.unregisterTimer(TIMER_NAME);
    this._enabled = false;
    this._updating = false;
    this._hasUpdatedOnce = false;
    this._updatingDeferred = Promise.withResolvers();
  }

  /**
   * Run a function while holding the update lock.
   *
   * This will prevent recipe updates from starting until after the callback finishes.
   *
   * @param {Function} fn The callback to call
   * @param {object} options Options to pass to the WebLocks request API.
   *
   * @returns {any} The return value of fn.
   */
  async withUpdateLock(fn, options) {
    return await locks.request(this.LOCK_ID, options, fn);
  }

  /**
   * Get all recipes from remote settings and update enrollments.
   *
   * If the RemoteSettingsExperimentLoader is already updating or disabled, this
   * function will not trigger an update.
   *
   * The actual update implementation is behind a WebLock. You can request the
   * lock `RemoteSettingsExperimentLoader.LOCK_ID` in order to pause updates.
   *
   * @param {string} trigger
   *                 The name of the event that triggered the update.
   * @param {object} options
   *                 Additional options. See `#updateImpl` docs for available
   *                 options.
   */
  async updateRecipes(trigger, options) {
    if (this._updating || !this._enabled) {
      return;
    }

    // If we've started shutting down, prevent an update from being triggered,
    // which we might not complete in time and could result in partial state
    // written to the database.
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      return;
    }

    this._updating = true;

    // If recipes have been updated once, replace the deferred with a new one so
    // that finishedUpdating() will not immediately resolve until we finish this
    // update.
    if (this._hasUpdatedOnce) {
      this._updatingDeferred = Promise.withResolvers();
    }

    await this.withUpdateLock(() => this.#updateImpl(trigger, options));

    this._hasUpdatedOnce = true;
    this._updating = false;
    this._updatingDeferred.resolve();

    this.recordIsReady();
  }

  /**
   * Get all recipes from Remote Settings and update enrollments.
   *
   * @param {string} trigger
   *                 The name of the event that triggered the update.
   * @param {object} options
   * @param {boolean} options.forceSync
   *                  Force a Remote Settings client to sync records before
   *                  updating. Otherwise locally cached records will be used.
   */
  async #updateImpl(trigger, { forceSync = false } = {}) {
    lazy.log.debug(`Updating recipes with trigger "${trigger ?? ""}"`);

    this.manager.optInRecipes = [];

    // The targeting context metrics do not work in artifact builds.
    // See-also: https://bugzilla.mozilla.org/show_bug.cgi?id=1936317
    // See-also: https://bugzilla.mozilla.org/show_bug.cgi?id=1936319
    if (lazy.TARGETING_CONTEXT_TELEMETRY_ENABLED) {
      lazy.recordTargetingContext();
    }

    // Since this method is async, the enabled pref could change between await
    // points. We don't want to half validate experiments, so we cache this to
    // keep it consistent throughout updating.
    const validationEnabled = this.validationEnabled;

    let recipeValidator;

    if (validationEnabled) {
      recipeValidator = new lazy.JsonSchema.Validator(
        await SCHEMAS.NimbusExperiment
      );
    }
    const { recipes: allRecipes, loadingError } =
      await this.getRecipesFromAllCollections({ forceSync, trigger });

    if (!loadingError) {
      const unenrolledExperimentSlugs = lazy.NimbusEnrollments
        .syncEnrollmentsEnabled
        ? await lazy.NimbusEnrollments.loadUnenrolledExperimentSlugsFromOtherProfiles()
        : undefined;

      const enrollmentsCtx = new EnrollmentsContext(
        this.manager,
        recipeValidator,
        {
          validationEnabled,
          shouldCheckTargeting: true,
          unenrolledExperimentSlugs,
        }
      );

      const { existingEnrollments, recipes } =
        this._partitionRecipes(allRecipes);

      for (const { enrollment, recipe } of existingEnrollments) {
        const result = recipe
          ? await enrollmentsCtx.checkRecipe(recipe)
          : CheckRecipeResult.Ok(MatchStatus.NOT_SEEN);

        await this.manager.updateEnrollment(
          enrollment,
          recipe,
          this.SOURCE,
          result
        );
      }

      for (const recipe of recipes) {
        const result = await enrollmentsCtx.checkRecipe(recipe);
        await this.manager.onRecipe(recipe, this.SOURCE, result);
      }

      lazy.log.debug(`${enrollmentsCtx.matches} recipes matched.`);
    }

    if (trigger !== "timer") {
      const lastUpdateTime = Math.round(Date.now() / 1000);
      Services.prefs.setIntPref(TIMER_LAST_UPDATE_PREF, lastUpdateTime);
    }

    Services.obs.notifyObservers(null, "nimbus:enrollments-updated");
  }

  /**
   * Return the recipes from all collections.
   *
   * The recipes will be filtered based on the allowed and disallowed feature
   * IDs.
   *
   * @see {@link getRecipesFromCollection}
   *
   * @param {object} options
   * @param {boolean} options.forceSync Whether or not to force a sync when
   * fetching recipes.
   * @param {string} options.trigger The name of the event that triggered the
   * update.
   *
   * @returns {Promise<{ recipes: object[]; loadingError: boolean; }>} The
   * recipes from Remote Settings.
   */
  async getRecipesFromAllCollections({ forceSync = false, trigger } = {}) {
    const recipes = [];
    let loadingError = false;

    const experiments = await this.getRecipesFromCollection({
      forceSync,
      client: this.remoteSettingsClients[EXPERIMENTS_COLLECTION],
      ...RS_COLLECTION_OPTIONS[EXPERIMENTS_COLLECTION],
    });

    if (experiments !== null) {
      recipes.push(...experiments);
    } else {
      loadingError = true;
    }

    const secureExperiments = await this.getRecipesFromCollection({
      forceSync,
      client: this.remoteSettingsClients[SECURE_EXPERIMENTS_COLLECTION],
      ...RS_COLLECTION_OPTIONS[SECURE_EXPERIMENTS_COLLECTION],
    });

    if (secureExperiments !== null) {
      recipes.push(...secureExperiments);
    } else {
      loadingError = true;
    }

    lazy.NimbusTelemetry.recordRemoteSettingsSync(
      forceSync,
      experiments,
      secureExperiments,
      trigger
    );

    return { recipes, loadingError };
  }

  /**
   * Return the recipes from a given collection.
   *
   * @param {object} options
   * @param {RemoteSettings} options.client
   *        The RemoteSettings client that will be used to fetch recipes.
   * @param {boolean} options.forceSync
   *        Force the RemoteSettings client to sync the collection before retrieving recipes.
   * @param {Set<string> | undefined} options.requiredFeatureIds
   *        If non-null, a recipe must include at least one feature in this set
   *        or it will be rejected.
   * @param {Set<string> | undefined} options.disallowedFeatureIds
   *        If a recipe uses any features in this list, it will be rejected.
   *
   * @returns {object[] | null}
   *          Recipes from the collection, filtered to match the allowed and
   *          disallowed feature IDs, or null if there was an error syncing the
   *          collection.
   */
  async getRecipesFromCollection({
    client,
    forceSync = false,
    requiredFeatureIds = undefined,
    disallowedFeatureIds = undefined,
  } = {}) {
    let recipes;
    try {
      recipes = await client.get({
        forceSync,
        emptyListFallback: false, // Throw instead of returning an empty list.
      });
      if (!Array.isArray(recipes)) {
        throw new Error("Remote Settings did not return an array");
      }
      if (
        recipes.length === 0 &&
        (await client.db.getLastModified()) === null
      ) {
        throw new Error(
          "Remote Settings returned an empty list but should have thrown (no last modified)"
        );
      }
      lazy.log.debug(
        `Got ${recipes.length} recipes from ${client.collectionName}`
      );
    } catch (e) {
      lazy.log.debug(
        `Error getting recipes from Remote Settings collection ${client.collectionName}: ${e}`
      );

      return null;
    }

    return recipes.filter(recipe => {
      if (
        requiredFeatureIds &&
        !recipe.featureIds.some(featureId => requiredFeatureIds.has(featureId))
      ) {
        lazy.log.warn(
          `Recipe ${recipe.slug} not returned from collection ${client.collectionName} because it does not contain at least one required feature ID.`
        );
        return false;
      }

      if (disallowedFeatureIds) {
        for (const featureId of recipe.featureIds) {
          if (disallowedFeatureIds.has(featureId)) {
            lazy.log.warn(
              `Recipe ${recipe.slug} not returned from collection ${client.collectionName} because it contains feature ${featureId}, which is disallowed for that collection.`
            );
            return false;
          }
        }
      }

      return true;
    });
  }

  async _optInToExperiment({
    slug,
    branch: branchSlug,
    collection,
    applyTargeting = false,
  }) {
    lazy.log.debug(`Attempting force enrollment with ${slug} / ${branchSlug}`);

    if (!lazy.NIMBUS_DEBUG) {
      lazy.log.debug(
        `Force enrollment only works when '${NIMBUS_DEBUG_PREF}' is enabled.`
      );
      // More generic error if no debug preference is on.
      throw new Error("Could not opt in.");
    }

    if (!lazy.ExperimentAPI.studiesEnabled) {
      lazy.log.debug(
        "Force enrollment does not work when studies are disabled."
      );
      throw new Error("Could not opt in: studies are disabled.");
    }

    let recipes;
    try {
      recipes = await lazy
        .RemoteSettings(collection || lazy.COLLECTION_ID)
        .get({
          // Throw instead of returning an empty list.
          emptyListFallback: false,
        });
    } catch (e) {
      console.error(e);
      throw new Error("Error getting recipes from remote settings.");
    }

    const recipe = recipes.find(r => r.slug === slug);

    if (!recipe) {
      throw new Error(
        `Could not find experiment slug ${slug} in collection ${
          collection || lazy.COLLECTION_ID
        }.`
      );
    }

    const recipeValidator = new lazy.JsonSchema.Validator(
      await SCHEMAS.NimbusExperiment
    );
    const enrollmentsCtx = new EnrollmentsContext(
      this.manager,
      recipeValidator,
      {
        validationEnabled: this.validationEnabled,
        shouldCheckTargeting: applyTargeting,
      }
    );

    // If a recipe is either targeting mismatch or invalid, ouput or throw the
    // specific error message.
    const result = await enrollmentsCtx.checkRecipe(recipe);
    if (!result.ok) {
      let errMsg = `${recipe.slug} failed validation with reason ${result.reason}`;

      switch (result.reason) {
        case lazy.NimbusTelemetry.ValidationFailureReason.INVALID_RECIPE:
          break;

        case lazy.NimbusTelemetry.ValidationFailureReason.INVALID_BRANCH:
          errMsg = `${errMsg}: branches ${result.branchSlugs.join(",")} failed validation`;
          break;

        case lazy.NimbusTelemetry.ValidationFailureReason.INVALID_FEATURE:
          errMsg = `${errMsg}: features ${result.featureIds.join(",")} do not exist`;
          break;

        case lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_ENTRY:
          errMsg = `${errMsg}: missing l10n entries ${result.missingL10nIds.join(",")} missing for locale ${result.locale}`;
          break;

        case lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_LOCALE:
          errMsg = `${errMsg}: missing localization for locale ${result.locale}`;
          break;

        case lazy.NimbusTelemetry.ValidationFailureReason.UNSUPPORTED_FEATURES:
          errMsg = `${errMsg}: features ${result.featureIds.join(",")} not supported by this application (${lazy.APP_ID})`;
          break;
      }

      lazy.log.error(errMsg);
      throw new Error(errMsg);
    }

    if (result.status === MatchStatus.NO_MATCH) {
      throw new Error(`Recipe ${recipe.slug} did not match targeting`);
    }

    const branch = recipe.branches.find(b => b.slug === branchSlug);
    if (!branch) {
      throw new Error(`Could not find branch slug ${branchSlug} in ${slug}`);
    }

    await this.manager.forceEnroll(recipe, branch);
  }

  /**
   * Handles feature status based on STUDIES_OPT_OUT_PREF.
   *
   * Changing this pref to false will turn off any recipe fetching and
   * processing.
   */
  async onEnabledPrefChange() {
    if (this._enabled && !lazy.ExperimentAPI.studiesEnabled) {
      this.disable();
    } else if (!this._enabled && lazy.ExperimentAPI.studiesEnabled) {
      // If the feature pref is turned on then turn on recipe processing.
      // If the opt in pref is turned on then turn on recipe processing only if
      // the feature pref is also enabled.
      await this.enable();
    }
  }

  /**
   * Sets a timer to update recipes every this.intervalInSeconds
   */
  setTimer() {
    if (!this._enabled) {
      // Don't enable the timer if we're disabled and the interval pref changes.
      return;
    }
    if (this.intervalInSeconds === 0) {
      // Used in tests where we want to turn this mechanism off
      lazy.timerManager.unregisterTimer(TIMER_NAME);
      return;
    }
    // The callbacks will be called soon after the timer is registered
    lazy.timerManager.registerTimer(
      TIMER_NAME,
      () => this.updateRecipes("timer", { forceSync: true }),
      this.intervalInSeconds
    );
    lazy.log.debug("Registered update timer");
  }

  recordIsReady() {
    const eventCount =
      lazy.NimbusFeatures.nimbusIsReady.getVariable("eventCount") ?? 1;
    for (let i = 0; i < eventCount; i++) {
      Glean.nimbusEvents.isReady.record();
    }
  }

  /**
   * Resolves when the RemoteSettingsExperimentLoader has updated at least once
   * and is not in the middle of an update.
   *
   * If studies are disabled or the RemoteSettingsExperimentLoader has been
   * disabled (i.e., during shutdown), then this will always resolve
   * immediately.
   */
  finishedUpdating() {
    if (!lazy.ExperimentAPI.studiesEnabled || !this._enabled) {
      return Promise.resolve();
    }

    return this._updatingDeferred.promise;
  }

  /**
   * Partition the given recipes into those that have existing enrollments and
   * those that don't
   *
   * @param {object[]} recipes
   *        The recipes returned from Remote Settings.
   *
   * @returns {object}
   *          An object containing:
   *
   *          - `existingEnrollments`, which is a list of all currently active
   *            enrollments from this source paired with the live recipe from
   *            `recipes` (if any);
   *
   *          - `recipes`, the remaining recipes which do not have currently
   *            active enrollments.
   */
  _partitionRecipes(recipes) {
    const rollouts = [];
    const experiments = [];

    const recipesBySlug = new Map(recipes.map(r => [r.slug, r]));

    for (const enrollment of this.manager.store.getAll()) {
      if (!enrollment.active || enrollment.source !== this.SOURCE) {
        continue;
      }

      const recipe = recipesBySlug.get(enrollment.slug);
      recipesBySlug.delete(enrollment.slug);

      if (enrollment.isRollout) {
        rollouts.push({ enrollment, recipe });
      } else {
        experiments.push({ enrollment, recipe });
      }
    }

    // Sort the rollouts and experiments by lastSeen (i.e., their enrollment
    // order).
    //
    // We want to review the rollouts before the experiments for
    // consistency with Nimbus SDK.
    function orderByLastSeen(a, b) {
      return new Date(a.enrollment.lastSeen) - new Date(b.enrollment.lastSeen);
    }

    rollouts.sort(orderByLastSeen);
    experiments.sort(orderByLastSeen);

    const existingEnrollments = rollouts;
    existingEnrollments.push(...experiments);

    // Skip over recipes not intended for desktop. Experimenter publishes
    // recipes into a collection per application (desktop goes to
    // `nimbus-desktop-experiments`) but all preview experiments share the same
    // collection (`nimbus-preview`).
    //
    // This is *not* the same as `lazy.APP_ID` which is used to distinguish
    // between desktop Firefox and the desktop background updater.
    const remaining = Array.from(recipesBySlug.values())
      .filter(r => r.appId === "firefox-desktop")
      .sort(
        (a, b) =>
          new Date(a.publishedDate ?? 0) - new Date(b.publishedDate ?? 0)
      );

    return {
      existingEnrollments,
      recipes: remaining,
    };
  }
}

export class EnrollmentsContext {
  constructor(
    manager,
    recipeValidator,
    {
      validationEnabled = true,
      shouldCheckTargeting = true,
      unenrolledExperimentSlugs,
    } = {}
  ) {
    this.manager = manager;
    this.recipeValidator = recipeValidator;

    this.validationEnabled = validationEnabled;
    this.validatorCache = {};
    this.shouldCheckTargeting = shouldCheckTargeting;
    this.unenrolledExperimentSlugs = unenrolledExperimentSlugs;
    this.matches = 0;

    this.locale = Services.locale.appLocaleAsBCP47;
  }

  async checkRecipe(recipe) {
    const validateFeatureSchemas =
      this.validationEnabled && !recipe.featureValidationOptOut;

    if (this.validationEnabled) {
      let validation = this.recipeValidator.validate(recipe);
      if (!validation.valid) {
        console.error(
          `Could not validate experiment recipe ${recipe.slug}: ${JSON.stringify(
            validation.errors,
            null,
            2
          )}`
        );
        if (recipe.slug) {
          lazy.NimbusTelemetry.recordValidationFailure(
            recipe.slug,
            lazy.NimbusTelemetry.ValidationFailureReason.INVALID_RECIPE
          );
        }

        return CheckRecipeResult.InvalidRecipe();
      }
    }

    // We don't include missing features here because if validation is enabled we report those errors later.
    const unsupportedFeatureIds = recipe.featureIds.filter(
      featureId =>
        Object.hasOwn(lazy.NimbusFeatures, featureId) &&
        !lazy.NimbusFeatures[featureId].applications.includes(lazy.APP_ID)
    );

    if (unsupportedFeatureIds.length) {
      // Do not record unsupported feature telemetry. This will only happen if
      // the background updater encounters a recipe with features it does not
      // support, which will happen with most recipes. Reporting these errors
      // results in an inordinate amount of telemetry being submitted.
      return CheckRecipeResult.UnsupportedFeatures();
    }

    if (recipe.isEnrollmentPaused) {
      lazy.log.debug(`${recipe.slug}: enrollment paused`);
      return CheckRecipeResult.Ok(MatchStatus.ENROLLMENT_PAUSED);
    }

    if (this.shouldCheckTargeting) {
      const match = await this.checkTargeting(recipe);

      if (match) {
        const type = recipe.isRollout ? "rollout" : "experiment";
        lazy.log.debug(`[${type}] ${recipe.slug} matched targeting`);
      } else {
        lazy.log.debug(`${recipe.slug} did not match due to targeting`);
        return CheckRecipeResult.Ok(MatchStatus.NO_MATCH);
      }
    }

    this.matches++;

    if (
      typeof recipe.localizations === "object" &&
      recipe.localizations !== null
    ) {
      if (
        typeof recipe.localizations[this.locale] !== "object" ||
        recipe.localizations[this.locale] === null
      ) {
        lazy.log.debug(
          `${recipe.slug} is localized but missing locale ${this.locale}`
        );
        lazy.NimbusTelemetry.recordValidationFailure(
          recipe.slug,
          lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_LOCALE,
          { locale: this.locale }
        );
        return CheckRecipeResult.MissingLocale(this.locale);
      }
    }

    const result = await this._validateBranches(recipe, validateFeatureSchemas);
    if (!result.ok) {
      lazy.log.debug(`${recipe.slug} did not validate: ${result.reason}`);
      return result;
    }

    if (!(await this.manager.isInBucketAllocation(recipe.bucketConfig))) {
      lazy.log.debug(`${recipe.slug} did not match bucket sampling`);
      return CheckRecipeResult.Ok(MatchStatus.TARGETING_ONLY);
    }

    if (!recipe.isRollout && this.unenrolledExperimentSlugs?.has(recipe.slug)) {
      return CheckRecipeResult.Ok(MatchStatus.UNENROLLED_IN_ANOTHER_PROFILE);
    }

    return CheckRecipeResult.Ok(MatchStatus.TARGETING_AND_BUCKETING);
  }

  async evaluateJexl(jexlString, customContext) {
    if (customContext && !customContext.experiment) {
      throw new Error(
        "Expected an .experiment property in second param of this function"
      );
    }

    if (!customContext.source) {
      throw new Error(
        "Expected a .source property that identifies which targeting expression is being evaluated."
      );
    }

    const context = lazy.TargetingContext.combineContexts(
      customContext,
      this.manager.createTargetingContext(),
      lazy.ASRouterTargeting.Environment
    );

    lazy.log.debug("Testing targeting expression:", jexlString);
    const targetingContext = new lazy.TargetingContext(context, {
      source: customContext.source,
    });

    let result = null;
    try {
      result = await targetingContext.evalWithDefault(jexlString);
    } catch (e) {
      lazy.log.debug("Targeting failed because of an error", e);
      console.error(e);
    }
    return result;
  }

  /**
   * Checks targeting of a recipe if it is defined
   * @param {Recipe} recipe
   * @param {{[key: string]: any}} customContext A custom filter context
   * @returns {Promise<boolean>} Should we process the recipe?
   */
  async checkTargeting(recipe) {
    if (!recipe.targeting) {
      lazy.log.debug(
        `No targeting for recipe ${recipe.slug}, so it matches automatically`
      );
      return true;
    }

    const result = await this.evaluateJexl(recipe.targeting, {
      experiment: recipe,
      source: recipe.slug,
    });

    return Boolean(result);
  }

  /**
   * Validate the branches of an experiment.
   *
   * @param {object} recipe The recipe object.
   * @param {boolean} validateSchema Whether to validate the feature values
   *        using JSON schemas.
   *
   * @returns {object} The lists of invalid branch slugs and invalid feature
   *                   IDs.
   */
  async _validateBranches({ slug, branches, localizations }, validateSchema) {
    const invalidBranchSlugs = [];
    const invalidFeatureIds = new Set();
    const missingL10nIds = new Set();

    if (
      validateSchema ||
      (typeof localizations === "object" && localizations !== null)
    ) {
      for (const [branchIdx, branch] of branches.entries()) {
        const features = branch.features ?? [branch.feature];
        for (const feature of features) {
          const { featureId, value } = feature;
          if (!lazy.NimbusFeatures[featureId]) {
            console.error(
              `Experiment ${slug} has unknown featureId: ${featureId}`
            );

            invalidFeatureIds.add(featureId);
            continue;
          }

          let substitutedValue = value;

          if (localizations) {
            // We already know that we have a localization table for this locale
            // because we checked in `checkRecipe`.
            try {
              substitutedValue =
                lazy._ExperimentFeature.substituteLocalizations(
                  value,
                  localizations[Services.locale.appLocaleAsBCP47],
                  missingL10nIds
                );
            } catch (e) {
              if (e?.reason === "l10n-missing-entry") {
                // Skip validation because it *will* fail.
                continue;
              }
              throw e;
            }
          }

          if (validateSchema) {
            let validator;
            if (this.validatorCache[featureId]) {
              validator = this.validatorCache[featureId];
            } else if (lazy.NimbusFeatures[featureId].manifest.schema?.uri) {
              const uri = lazy.NimbusFeatures[featureId].manifest.schema.uri;
              try {
                const schema = await fetch(uri, {
                  credentials: "omit",
                }).then(rsp => rsp.json());

                validator = this.validatorCache[featureId] =
                  new lazy.JsonSchema.Validator(schema);
              } catch (e) {
                throw new Error(
                  `Could not fetch schema for feature ${featureId} at "${uri}": ${e}`
                );
              }
            } else {
              const schema = this._generateVariablesOnlySchema(
                lazy.NimbusFeatures[featureId]
              );
              validator = this.validatorCache[featureId] =
                new lazy.JsonSchema.Validator(schema);
            }

            const result = validator.validate(substitutedValue);
            if (!result.valid) {
              console.error(
                `Experiment ${slug} branch ${branchIdx} feature ${featureId} does not validate: ${JSON.stringify(
                  result.errors,
                  undefined,
                  2
                )}`
              );
              invalidBranchSlugs.push(branch.slug);
            }
          }
        }
      }
    }

    if (invalidBranchSlugs.length) {
      for (const branchSlug of invalidBranchSlugs) {
        lazy.NimbusTelemetry.recordValidationFailure(
          slug,
          lazy.NimbusTelemetry.ValidationFailureReason.INVALID_BRANCH,
          {
            branch: branchSlug,
          }
        );
      }

      return CheckRecipeResult.InvalidBranches(invalidBranchSlugs);
    }

    if (invalidFeatureIds.size) {
      // Do not record invalid feature telemetry. In practice this only happens
      // due to long-lived recipes referencing features that were removed in a
      // prior version. Reporting these errors results in an inordinate amount
      // of telemetry being submitted.
      return CheckRecipeResult.InvalidFeatures(Array.from(invalidFeatureIds));
    }

    if (missingL10nIds.size) {
      lazy.NimbusTelemetry.recordValidationFailure(
        slug,
        lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_ENTRY,
        {
          locale: this.locale,
          l10nIds: Array.from(missingL10nIds).join(","),
        }
      );

      return CheckRecipeResult.MissingL10nEntry(
        this.locale,
        Array.from(missingL10nIds)
      );
    }

    // We have only performed targeting and not bucketing, so technically we're
    // in a TARGETING_ONLY scenario, but our caller only cares about the error
    // case anyway.
    return CheckRecipeResult.Ok(null);
  }

  _generateVariablesOnlySchema({ featureId, manifest }) {
    // See-also: https://github.com/mozilla/experimenter/blob/main/app/experimenter/features/__init__.py#L21-L64
    const schema = {
      $schema: "https://json-schema.org/draft/2019-09/schema",
      title: featureId,
      description: manifest.description,
      type: "object",
      properties: {},
      additionalProperties: true,
    };

    for (const [varName, desc] of Object.entries(manifest.variables)) {
      const prop = {};
      switch (desc.type) {
        case "boolean":
        case "string":
          prop.type = desc.type;
          break;

        case "int":
          prop.type = "integer";
          break;

        case "json":
          // NB: Don't set a type of json fields, since they can be of any type.
          break;

        default:
          // NB: Experimenter doesn't outright reject invalid types either.
          console.error(
            `Feature ID ${featureId} has variable ${varName} with invalid FML type: ${prop.type}`
          );
          break;
      }

      if (prop.type === "string" && !!desc.enum) {
        prop.enum = [...desc.enum];
      }

      schema.properties[varName] = prop;
    }

    return schema;
  }
}
