package com.t850.engine;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import org.json.JSONObject;

@SuppressWarnings("deprecation")
public final class LauncherActivity extends Activity {
    public static final String EXTRA_SCENE = "com.t850.engine.extra.SCENE";
    public static final String EXTRA_MODEL = "com.t850.engine.extra.MODEL";
    public static final String EXTRA_SCENE_FILE = "com.t850.engine.extra.SCENE_FILE";
    public static final String EXTRA_LOG_LEVEL = "com.t850.engine.extra.LOG_LEVEL";
    public static final String EXTRA_AUTO_RUN = "com.t850.engine.extra.AUTO_RUN";
    public static final String EXTRA_DUMP_FRAME = "com.t850.engine.extra.DUMP_FRAME";
    public static final String EXTRA_DUMP_SECONDS = "com.t850.engine.extra.DUMP_SECONDS";
    public static final String EXTRA_DEBUG_FRAMES = "com.t850.engine.extra.DEBUG_FRAMES";
    public static final String EXTRA_KEEP_RUNNING = "com.t850.engine.extra.KEEP_RUNNING";
    public static final String EXTRA_REPLAY_SNAPSHOT = "com.t850.engine.extra.REPLAY_SNAPSHOT";
    public static final String EXTRA_PROFILE = "com.t850.engine.extra.PROFILE";
    public static final String EXTRA_PROFILE_FRAMES = "com.t850.engine.extra.PROFILE_FRAMES";
    public static final String EXTRA_BENCHMARK = "com.t850.engine.extra.BENCHMARK";
    public static final String EXTRA_BENCHMARK_MATRIX = "com.t850.engine.extra.BENCHMARK_MATRIX";
    public static final String EXTRA_BENCHMARK_OUTPUT = "com.t850.engine.extra.BENCHMARK_OUTPUT";
    public static final String EXTRA_BENCHMARK_REPORT = "com.t850.engine.extra.BENCHMARK_REPORT";
    public static final String EXTRA_BENCHMARK_SECONDS = "com.t850.engine.extra.BENCHMARK_SECONDS";
    public static final String EXTRA_BENCHMARK_FRAMES = "com.t850.engine.extra.BENCHMARK_FRAMES";
    public static final String EXTRA_BENCHMARK_FIXED_DT = "com.t850.engine.extra.BENCHMARK_FIXED_DT";
    public static final String EXTRA_WIDTH = "com.t850.engine.extra.WIDTH";
    public static final String EXTRA_HEIGHT = "com.t850.engine.extra.HEIGHT";
    public static final String EXTRA_OFFSCREEN = "com.t850.engine.extra.OFFSCREEN";
    public static final String EXTRA_AUTO_START_RAGDOLL = "com.t850.engine.extra.AUTO_START_RAGDOLL";
    public static final String EXTRA_RAGDOLL_SPEED_INDEX = "com.t850.engine.extra.RAGDOLL_SPEED_INDEX";
    public static final String EXTRA_RETURN_TO_NATIVE = "com.t850.engine.extra.RETURN_TO_NATIVE";
    public static final String EXTRA_RUN_ID = "com.t850.engine.extra.RUN_ID";
    public static final String EXTRA_SCENE_PROFILE = "com.t850.engine.extra.SCENE_PROFILE";

    private static final String PREFS_NAME = "t850_launcher";
    private static final String PREF_SCENE = "scene";
    private static final String PREF_MODEL = "model";
    private static final String PREF_SCENE_FILE = "sceneFile";
    private static final String PREF_SANDBOX_CONTENT = "sandboxContent";
    private static final String PREF_LOG_LEVEL = "logLevel";
    private static final String PREF_RETURN_TO_NATIVE = "returnToNative";
    private static final String PREF_CONSUMED_AUTO_RUN = "consumedAutoRun";
    private static final int REQUEST_NATIVE_SCENE = 1;
    private static final int CONTENT_SCENE_FILE = 0;
    private static final int CONTENT_MODEL = 1;

    private final List<Option> scenes = new ArrayList<>();
    private final List<Option> sandboxContentOptions = new ArrayList<>();
    private final List<Option> logLevels = new ArrayList<>();
    private final List<AssetOption> sceneFiles = new ArrayList<>();
    private final List<AssetOption> models = new ArrayList<>();

    private Spinner sceneSpinner;
    private Spinner sandboxContentSpinner;
    private Spinner sceneFileSpinner;
    private Spinner modelSpinner;
    private Spinner logSpinner;
    private TextView sandboxContentLabel;
    private TextView sceneFileLabel;
    private TextView sceneFileHint;
    private TextView modelLabel;
    private TextView modelHint;
    private TextView benchmarkStatus;
    private TextView benchmarkReport;
    private boolean launchingNative;
    private static boolean resumeNativeInCurrentProcess;
    private final List<BenchmarkRun> benchmarkRuns = new ArrayList<>();
    private final List<BenchmarkResult> benchmarkResults = new ArrayList<>();
    private int benchmarkRunIndex = -1;
    private boolean benchmarkMatrixRunning;

    private static final class Option {
        final String label;
        final int value;

        Option(String label, int value) {
            this.label = label;
            this.value = value;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    private static final class AssetOption {
        final String path;
        final String label;

        AssetOption(String path, String displayRoot) {
            this.path = path;
            this.label = path.startsWith(displayRoot) ? path.substring(displayRoot.length()) : path;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    private static final class NativeLaunchOptions {
        int scene;
        String model;
        int logLevel;
        int dumpFrame = -1;
        float dumpSeconds = -1.0f;
        boolean debugFrames;
        boolean keepRunning;
        boolean profile;
        int profileFrames = 300;
        boolean autoStartRagdoll;
        int ragdollSpeedIndex = -1;
        boolean returnToNative;
        String replaySnapshot;
        int sandboxContent = CONTENT_SCENE_FILE;
        String sceneFile;
        String sceneProfile;
        boolean benchmark;
        boolean benchmarkMatrix;
        String benchmarkOutput;
        String benchmarkReport;
        int benchmarkSeconds;
        int benchmarkFrames;
        float benchmarkFixedDt;
        int width;
        int height;
        boolean offscreen;
    }

    private static final class BenchmarkRun {
        final int width;
        final int height;
        final boolean offscreen;
        final File outputFile;

        BenchmarkRun(int width, int height, boolean offscreen, File outputFile) {
            this.width = width;
            this.height = height;
            this.offscreen = offscreen;
            this.outputFile = outputFile;
        }

        String resolution() {
            return width + "x" + height;
        }

        String mode() {
            return offscreen ? "offscreen" : "onscreen";
        }
    }

    private static final class BenchmarkResult {
        final BenchmarkRun run;
        final double averageFps;
        final double medianFps;
        final double minFps;
        final double maxFps;
        final int frameCount;
        final double durationSeconds;

        BenchmarkResult(BenchmarkRun run, double averageFps, double medianFps, double minFps, double maxFps,
                        int frameCount, double durationSeconds) {
            this.run = run;
            this.averageFps = averageFps;
            this.medianFps = medianFps;
            this.minFps = minFps;
            this.maxFps = maxFps;
            this.frameCount = frameCount;
            this.durationSeconds = durationSeconds;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        scenes.add(new Option("Sandbox", 0));
        scenes.add(new Option("Day Scene", 1));
        scenes.add(new Option("Quake3 Mock", 2));
        scenes.add(new Option("Ragdoll Editor", 3));
        scenes.add(new Option("Scene Template", 4));

        sandboxContentOptions.add(new Option("Scene file (.t8scene)", CONTENT_SCENE_FILE));
        sandboxContentOptions.add(new Option("Model file", CONTENT_MODEL));

        logLevels.add(new Option("Error", 0));
        logLevels.add(new Option("Info", 1));
        logLevels.add(new Option("Debug", 2));
        logLevels.add(new Option("Verbose", 3));
        logLevels.add(new Option("Trace", 4));

        loadSceneFiles();
        loadModels();
        buildUi();
        restoreSelections();
        updateSandboxContentControls();
        handleAutoRunIntent(getIntent());
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumeNativeSceneIfNeeded();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleAutoRunIntent(intent);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_NATIVE_SCENE) {
            launchingNative = false;
            if (benchmarkMatrixRunning) {
                handleBenchmarkRunReturned();
                return;
            }
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                    .edit()
                    .remove(PREF_CONSUMED_AUTO_RUN)
                    .apply();
        }
    }

    @Override
    public void onBackPressed() {
        resumeNativeInCurrentProcess = false;
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit()
                .putBoolean(PREF_RETURN_TO_NATIVE, false)
                .remove(PREF_CONSUMED_AUTO_RUN)
                .apply();
        finishAndRemoveTask();
    }

    private void buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(17, 24, 39));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(20), dp(24), dp(24));
        scroll.addView(root, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText(getApplicationInfo().loadLabel(getPackageManager()) + " Launcher");
        title.setTextColor(Color.rgb(224, 231, 255));
        title.setTextSize(26);
        title.setPadding(0, 0, 0, dp(4));
        root.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Select a native scene, then a packaged .t8scene or model for Sandbox.");
        subtitle.setTextColor(Color.rgb(156, 163, 175));
        subtitle.setTextSize(14);
        subtitle.setPadding(0, 0, 0, dp(20));
        root.addView(subtitle);

        root.addView(label("Graphics API"));
        TextView api = valueText("Vulkan (fixed)");
        api.setEnabled(false);
        root.addView(api);

        root.addView(spacer(12));
        root.addView(label("Native scene"));
        sceneSpinner = spinner(scenes);
        root.addView(sceneSpinner);

        root.addView(spacer(12));
        sandboxContentLabel = label("Sandbox content");
        root.addView(sandboxContentLabel);
        sandboxContentSpinner = spinner(sandboxContentOptions);
        root.addView(sandboxContentSpinner);

        root.addView(spacer(12));
        sceneFileLabel = label("Scene file (.t8scene)");
        root.addView(sceneFileLabel);
        sceneFileSpinner = spinner(sceneFiles);
        root.addView(sceneFileSpinner);
        sceneFileHint = hint("Scene files are read from the packaged Assets/Scenes directory.");
        root.addView(sceneFileHint);

        root.addView(spacer(12));
        modelLabel = label("Model (Sandbox)");
        root.addView(modelLabel);
        modelSpinner = spinner(models);
        root.addView(modelSpinner);
        modelHint = hint("Models are read from packaged or downloaded Assets/Models directories.");
        root.addView(modelHint);

        root.addView(spacer(12));
        root.addView(label("ADB log verbosity"));
        logSpinner = spinner(logLevels);
        root.addView(logSpinner);
        root.addView(hint("View logs with: adb logcat -s T850"));

        Button runButton = new Button(this);
        runButton.setText("Run");
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        buttonParams.setMargins(0, dp(24), 0, 0);
        root.addView(runButton, buttonParams);

        Button benchmarkButton = new Button(this);
        benchmarkButton.setText("Run DayScene Benchmark Matrix");
        root.addView(benchmarkButton, buttonParams);

        benchmarkStatus = hint("Benchmark matrix: Vulkan, 1920x1080 / 2560x1440 / 3840x2160, onscreen and offscreen.");
        root.addView(benchmarkStatus);
        benchmarkReport = valueText("");
        benchmarkReport.setTypeface(Typeface.MONOSPACE);
        benchmarkReport.setVisibility(View.GONE);
        root.addView(benchmarkReport);

        sceneSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                int scene = selectedScene().value;
                if (sceneUsesSceneFile(scene)) {
                    selectSceneFile(defaultSceneFilePath(scene));
                }
                updateSandboxContentControls();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                updateSandboxContentControls();
            }
        });

        sandboxContentSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                updateSandboxContentControls();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                updateSandboxContentControls();
            }
        });

        runButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                runNativeScene();
            }
        });

        benchmarkButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startBenchmarkMatrix();
            }
        });

        setContentView(scroll);
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(167, 139, 250));
        view.setTextSize(12);
        view.setPadding(0, 0, 0, dp(4));
        return view;
    }

    private TextView valueText(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(229, 231, 235));
        view.setTextSize(16);
        view.setPadding(dp(12), dp(10), dp(12), dp(10));
        view.setBackgroundColor(Color.rgb(31, 41, 55));
        return view;
    }

    private TextView hint(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(156, 163, 175));
        view.setTextSize(12);
        view.setPadding(0, dp(4), 0, 0);
        return view;
    }

    private View spacer(int heightDp) {
        View view = new View(this);
        view.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(heightDp)));
        return view;
    }

    private <T> Spinner spinner(List<T> items) {
        Spinner spinner = new Spinner(this);
        ArrayAdapter<T> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, items);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        return spinner;
    }

    private void restoreSelections() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        selectOption(sceneSpinner, scenes, prefs.getInt(PREF_SCENE, 0));
        selectOption(sandboxContentSpinner, sandboxContentOptions, prefs.getInt(PREF_SANDBOX_CONTENT, CONTENT_SCENE_FILE));
        selectSceneFile(prefs.getString(PREF_SCENE_FILE, defaultSceneFilePath()));
        selectModel(prefs.getString(PREF_MODEL, defaultModelPath()));
        selectOption(logSpinner, logLevels, prefs.getInt(PREF_LOG_LEVEL, 2));
    }

    private void selectOption(Spinner spinner, List<Option> options, int value) {
        for (int i = 0; i < options.size(); ++i) {
            if (options.get(i).value == value) {
                spinner.setSelection(i);
                return;
            }
        }
    }

    private void selectSceneFile(String path) {
        if (!selectAsset(sceneFileSpinner, sceneFiles, path)) {
            selectAsset(sceneFileSpinner, sceneFiles, defaultSceneFilePath());
        }
    }

    private void selectModel(String path) {
        selectAsset(modelSpinner, models, path);
    }

    private boolean selectAsset(Spinner spinner, List<AssetOption> options, String path) {
        for (int i = 0; i < options.size(); ++i) {
            if (options.get(i).path.equals(path)) {
                spinner.setSelection(i);
                return true;
            }
        }
        if (!options.isEmpty()) {
            spinner.setSelection(0);
        }
        return false;
    }

    private void updateSandboxContentControls() {
        int scene = selectedScene().value;
        boolean sandbox = scene == 0;
        boolean sceneFile = sceneUsesSceneFile(scene) || (sandbox && selectedSandboxContent().value == CONTENT_SCENE_FILE);
        boolean model = sceneUsesModel(scene) || (sandbox && selectedSandboxContent().value == CONTENT_MODEL);
        sandboxContentLabel.setEnabled(sandbox);
        sandboxContentSpinner.setEnabled(sandbox);
        sceneFileLabel.setEnabled(sceneFile);
        sceneFileSpinner.setEnabled(sceneFile);
        sceneFileHint.setEnabled(sceneFile);
        modelLabel.setEnabled(model);
        modelSpinner.setEnabled(model);
        modelHint.setEnabled(model);
    }

    private void runNativeScene() {
        Option scene = selectedScene();
        Option sandboxContent = selectedSandboxContent();
        Option logLevel = selectedLogLevel();
        AssetOption sceneFile = selectedSceneFile();
        AssetOption model = selectedModel();
        boolean launchSceneFile = sceneUsesSceneFile(scene.value) ||
                (scene.value == 0 && sandboxContent.value == CONTENT_SCENE_FILE);
        boolean launchModel = sceneUsesModel(scene.value) ||
                (scene.value == 0 && sandboxContent.value == CONTENT_MODEL);

        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit()
                .putInt(PREF_SCENE, scene.value)
                .putInt(PREF_SANDBOX_CONTENT, sandboxContent.value)
                .putString(PREF_SCENE_FILE, sceneFile.path)
                .putString(PREF_MODEL, model.path)
                .putInt(PREF_LOG_LEVEL, logLevel.value)
                .putBoolean(PREF_RETURN_TO_NATIVE, true)
                .apply();
        resumeNativeInCurrentProcess = true;

        NativeLaunchOptions options = new NativeLaunchOptions();
        options.scene = scene.value;
        options.sandboxContent = sandboxContent.value;
        if (launchSceneFile) {
            options.sceneFile = sceneFile.path;
        } else if (launchModel) {
            options.model = model.path;
        }
        options.logLevel = logLevel.value;
        options.returnToNative = true;
        launchNativeScene(options);
    }

    private void resumeNativeSceneIfNeeded() {
        if (launchingNative) {
            return;
        }
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        if (!prefs.getBoolean(PREF_RETURN_TO_NATIVE, false)) {
            resumeNativeInCurrentProcess = false;
            return;
        }
        resumeNativeInCurrentProcess = true;
        launchingNative = false;

        int scene = prefs.getInt(PREF_SCENE, 0);
        int sandboxContent = prefs.getInt(PREF_SANDBOX_CONTENT, CONTENT_SCENE_FILE);
        String sceneFile = prefs.getString(PREF_SCENE_FILE, defaultSceneFilePath());
        String model = prefs.getString(PREF_MODEL, defaultModelPath());
        int logLevel = prefs.getInt(PREF_LOG_LEVEL, 2);
        NativeLaunchOptions options = new NativeLaunchOptions();
        options.scene = scene;
        options.sandboxContent = sandboxContent;
        if (sceneUsesSceneFile(scene) || (scene == 0 && sandboxContent == CONTENT_SCENE_FILE)) {
            options.sceneFile = sceneFile;
        } else if (sceneUsesModel(scene) || scene == 0) {
            options.model = model;
        }
        options.logLevel = logLevel;
        options.returnToNative = true;
        launchNativeScene(options);
    }

    private void handleAutoRunIntent(Intent intent) {
        if (intent == null || !intent.getBooleanExtra(EXTRA_AUTO_RUN, false) || launchingNative) {
            return;
        }

        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        String autoRunKey = intent.getStringExtra(EXTRA_RUN_ID);
        if (autoRunKey == null || autoRunKey.isEmpty()) {
            autoRunKey = intent.toUri(0);
        }
        if (autoRunKey.equals(prefs.getString(PREF_CONSUMED_AUTO_RUN, null))) {
            intent.removeExtra(EXTRA_AUTO_RUN);
            setIntent(intent);
            boolean shouldReturnToNative = prefs.getBoolean(PREF_RETURN_TO_NATIVE, false);
            prefs.edit()
                    .remove(PREF_CONSUMED_AUTO_RUN)
                    .commit();
            if (shouldReturnToNative) {
                resumeNativeSceneIfNeeded();
            }
            return;
        }
        intent.removeExtra(EXTRA_AUTO_RUN);
        setIntent(intent);

        NativeLaunchOptions options = new NativeLaunchOptions();
        options.scene = intent.getIntExtra(EXTRA_SCENE, prefs.getInt(PREF_SCENE, 0));
        if (options.scene < 0 || options.scene > 4) {
            options.scene = 0;
        }
        int fallbackSandboxContent = prefs.getInt(PREF_SANDBOX_CONTENT, CONTENT_SCENE_FILE);
        String fallbackSceneFile = prefs.getString(PREF_SCENE_FILE, defaultSceneFilePath());
        String fallbackModel = prefs.getString(PREF_MODEL, defaultModelPath());
        options.model = intent.getStringExtra(EXTRA_MODEL);
        boolean explicitModel = options.model != null && !options.model.isEmpty();
        if (!explicitModel) {
            options.model = fallbackModel;
        }
        options.sceneFile = intent.getStringExtra(EXTRA_SCENE_FILE);
        if (options.sceneFile != null && !options.sceneFile.isEmpty()) {
            if (!sceneUsesSceneFile(options.scene)) {
                options.scene = 0;
            }
            options.model = null;
            options.sandboxContent = CONTENT_SCENE_FILE;
        } else {
            options.sceneFile = fallbackSceneFile;
            options.sandboxContent = explicitModel ? CONTENT_MODEL : fallbackSandboxContent;
        }
        if (sceneUsesSceneFile(options.scene) || (options.scene == 0 && options.sandboxContent == CONTENT_SCENE_FILE)) {
            options.model = null;
        } else {
            options.sceneFile = null;
        }
        options.logLevel = intent.getIntExtra(EXTRA_LOG_LEVEL, prefs.getInt(PREF_LOG_LEVEL, 2));
        if (options.logLevel < 0 || options.logLevel > 4) {
            options.logLevel = 2;
        }
        options.dumpFrame = intent.getIntExtra(EXTRA_DUMP_FRAME, -1);
        options.dumpSeconds = intent.getFloatExtra(EXTRA_DUMP_SECONDS, -1.0f);
        options.debugFrames = intent.getBooleanExtra(EXTRA_DEBUG_FRAMES, false);
        options.keepRunning = intent.getBooleanExtra(EXTRA_KEEP_RUNNING, false);
        options.profile = intent.getBooleanExtra(EXTRA_PROFILE, false);
        options.profileFrames = intent.getIntExtra(EXTRA_PROFILE_FRAMES, 300);
        options.autoStartRagdoll = intent.getBooleanExtra(EXTRA_AUTO_START_RAGDOLL, false);
        options.ragdollSpeedIndex = intent.getIntExtra(EXTRA_RAGDOLL_SPEED_INDEX, -1);
        options.replaySnapshot = intent.getStringExtra(EXTRA_REPLAY_SNAPSHOT);
        options.sceneProfile = intent.getStringExtra(EXTRA_SCENE_PROFILE);
        options.benchmark = intent.getBooleanExtra(EXTRA_BENCHMARK, false);
        options.benchmarkMatrix = intent.getBooleanExtra(EXTRA_BENCHMARK_MATRIX, false);
        options.benchmarkOutput = intent.getStringExtra(EXTRA_BENCHMARK_OUTPUT);
        options.benchmarkReport = intent.getStringExtra(EXTRA_BENCHMARK_REPORT);
        options.benchmarkSeconds = intent.getIntExtra(EXTRA_BENCHMARK_SECONDS, 0);
        options.benchmarkFrames = intent.getIntExtra(EXTRA_BENCHMARK_FRAMES, 0);
        options.benchmarkFixedDt = intent.getFloatExtra(EXTRA_BENCHMARK_FIXED_DT, 0.0f);
        options.width = intent.getIntExtra(EXTRA_WIDTH, 0);
        options.height = intent.getIntExtra(EXTRA_HEIGHT, 0);
        options.offscreen = intent.getBooleanExtra(EXTRA_OFFSCREEN, false);
        options.returnToNative = intent.getBooleanExtra(EXTRA_RETURN_TO_NATIVE, false);

        selectOption(sceneSpinner, scenes, options.scene);
        selectOption(sandboxContentSpinner, sandboxContentOptions, options.sandboxContent);
        if (options.sceneFile != null) {
            selectSceneFile(options.sceneFile);
        }
        if (options.model != null) {
            selectModel(options.model);
        }
        selectOption(logSpinner, logLevels, options.logLevel);
        updateSandboxContentControls();

        prefs.edit()
                .putInt(PREF_SCENE, options.scene)
                .putInt(PREF_SANDBOX_CONTENT, options.sandboxContent)
                .putString(PREF_SCENE_FILE, options.sceneFile != null ? options.sceneFile : fallbackSceneFile)
                .putString(PREF_MODEL, options.model != null ? options.model : fallbackModel)
                .putInt(PREF_LOG_LEVEL, options.logLevel)
                .putBoolean(PREF_RETURN_TO_NATIVE, options.returnToNative)
                .putString(PREF_CONSUMED_AUTO_RUN, autoRunKey)
                .commit();
        resumeNativeInCurrentProcess = options.returnToNative;

        launchNativeScene(options);
    }

    @SuppressWarnings("deprecation")
    private void launchNativeScene(NativeLaunchOptions options) {
        launchingNative = true;
        Intent intent = new Intent();
        intent.setClassName(getPackageName(), "android.app.NativeActivity");
        intent.putExtra(EXTRA_SCENE, options.scene);
        intent.putExtra(EXTRA_LOG_LEVEL, options.logLevel);
        if (options.sceneFile != null && !options.sceneFile.isEmpty()) {
            intent.putExtra(EXTRA_SCENE_FILE, options.sceneFile);
        } else if (options.model != null && !options.model.isEmpty()) {
            intent.putExtra(EXTRA_MODEL, options.model);
        }
        if (options.dumpFrame >= 0) {
            intent.putExtra(EXTRA_DUMP_FRAME, options.dumpFrame);
        }
        if (options.dumpSeconds >= 0.0f) {
            intent.putExtra(EXTRA_DUMP_SECONDS, options.dumpSeconds);
        }
        if (options.debugFrames) {
            intent.putExtra(EXTRA_DEBUG_FRAMES, true);
        }
        if (options.keepRunning) {
            intent.putExtra(EXTRA_KEEP_RUNNING, true);
        }
        if (options.profile) {
            intent.putExtra(EXTRA_PROFILE, true);
            intent.putExtra(EXTRA_PROFILE_FRAMES, Math.max(1, options.profileFrames));
        }
        if (options.benchmark) {
            intent.putExtra(EXTRA_BENCHMARK, true);
        }
        if (options.benchmarkMatrix) {
            intent.putExtra(EXTRA_BENCHMARK_MATRIX, true);
        }
        if (options.benchmarkOutput != null && !options.benchmarkOutput.isEmpty()) {
            intent.putExtra(EXTRA_BENCHMARK_OUTPUT, options.benchmarkOutput);
        }
        if (options.benchmarkReport != null && !options.benchmarkReport.isEmpty()) {
            intent.putExtra(EXTRA_BENCHMARK_REPORT, options.benchmarkReport);
        }
        if (options.benchmarkSeconds > 0) {
            intent.putExtra(EXTRA_BENCHMARK_SECONDS, options.benchmarkSeconds);
        }
        if (options.benchmarkFrames > 0) {
            intent.putExtra(EXTRA_BENCHMARK_FRAMES, options.benchmarkFrames);
        }
        if (options.benchmarkFixedDt > 0.0f) {
            intent.putExtra(EXTRA_BENCHMARK_FIXED_DT, options.benchmarkFixedDt);
        }
        if (options.width > 0) {
            intent.putExtra(EXTRA_WIDTH, options.width);
        }
        if (options.height > 0) {
            intent.putExtra(EXTRA_HEIGHT, options.height);
        }
        if (options.offscreen) {
            intent.putExtra(EXTRA_OFFSCREEN, true);
        }
        if (options.autoStartRagdoll) {
            intent.putExtra(EXTRA_AUTO_START_RAGDOLL, true);
        }
        if (options.ragdollSpeedIndex >= 0) {
            intent.putExtra(EXTRA_RAGDOLL_SPEED_INDEX, options.ragdollSpeedIndex);
        }
        if (options.replaySnapshot != null && !options.replaySnapshot.isEmpty()) {
            intent.putExtra(EXTRA_REPLAY_SNAPSHOT, options.replaySnapshot);
        }
        if (options.sceneProfile != null && !options.sceneProfile.isEmpty()) {
            intent.putExtra(EXTRA_SCENE_PROFILE, options.sceneProfile);
        }
        if (options.returnToNative) {
            intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            startActivity(intent);
            finish();
        } else {
            startActivityForResult(intent, REQUEST_NATIVE_SCENE);
        }
    }

    private Option selectedScene() {
        return (Option) sceneSpinner.getSelectedItem();
    }

    private Option selectedLogLevel() {
        return (Option) logSpinner.getSelectedItem();
    }

    private Option selectedSandboxContent() {
        Object selected = sandboxContentSpinner.getSelectedItem();
        if (selected instanceof Option) {
            return (Option) selected;
        }
        return sandboxContentOptions.isEmpty()
                ? new Option("Scene file (.t8scene)", CONTENT_SCENE_FILE)
                : sandboxContentOptions.get(0);
    }

    private boolean sceneUsesSceneFile(int scene) {
        return scene == 2 || scene == 4;
    }

    private boolean sceneUsesModel(int scene) {
        return scene == 3;
    }

    private void startBenchmarkMatrix() {
        File root = new File(getExternalFilesDir(null), "benchmarks/dayscene_" + System.currentTimeMillis());
        if (!root.mkdirs() && !root.isDirectory()) {
            Toast.makeText(this, "Could not create benchmark directory", Toast.LENGTH_LONG).show();
            return;
        }

        benchmarkReport.setVisibility(View.GONE);
        benchmarkStatus.setText("Benchmark matrix running in DayScene...");

        NativeLaunchOptions options = new NativeLaunchOptions();
        options.scene = 1;
        options.logLevel = selectedLogLevel().value;
        options.benchmark = true;
        options.benchmarkMatrix = true;
        options.benchmarkReport = new File(root, "DayScene_Benchmark_Report.md").getAbsolutePath();
        options.benchmarkSeconds = 90;
        options.returnToNative = false;
        launchNativeScene(options);
    }

    private void launchCurrentBenchmarkRun() {
        if (benchmarkRunIndex < 0 || benchmarkRunIndex >= benchmarkRuns.size()) {
            benchmarkMatrixRunning = false;
            benchmarkStatus.setText("Benchmark matrix complete.");
            benchmarkReport.setText(buildBenchmarkReport());
            benchmarkReport.setVisibility(View.VISIBLE);
            return;
        }

        BenchmarkRun run = benchmarkRuns.get(benchmarkRunIndex);
        benchmarkStatus.setText(String.format(Locale.US,
                "Running %d/%d: Vulkan %s %s",
                benchmarkRunIndex + 1,
                benchmarkRuns.size(),
                run.resolution(),
                run.mode()));
        benchmarkReport.setVisibility(View.GONE);

        NativeLaunchOptions options = new NativeLaunchOptions();
        options.scene = 1;
        options.logLevel = selectedLogLevel().value;
        options.benchmark = true;
        options.benchmarkOutput = run.outputFile.getAbsolutePath();
        options.width = run.width;
        options.height = run.height;
        options.offscreen = run.offscreen;
        options.returnToNative = false;
        launchNativeScene(options);
    }

    private void handleBenchmarkRunReturned() {
        if (benchmarkRunIndex >= 0 && benchmarkRunIndex < benchmarkRuns.size()) {
            BenchmarkRun run = benchmarkRuns.get(benchmarkRunIndex);
            try {
                benchmarkResults.add(readBenchmarkResult(run));
            } catch (Exception ex) {
                benchmarkMatrixRunning = false;
                benchmarkStatus.setText("Benchmark failed: " + ex.getMessage());
                benchmarkReport.setVisibility(View.GONE);
                return;
            }
        }
        benchmarkRunIndex++;
        launchCurrentBenchmarkRun();
    }

    private BenchmarkResult readBenchmarkResult(BenchmarkRun run) throws Exception {
        if (!run.outputFile.isFile()) {
            throw new IOException("Missing result " + run.outputFile.getName());
        }
        String text = readTextFile(run.outputFile);
        JSONObject json = new JSONObject(text);
        JSONObject fps = json.optJSONObject("statsFps");
        JSONObject ms = json.optJSONObject("statsMs");
        double averageFps = fps != null ? fps.optDouble("average", 0.0) : fpsFromMs(ms, "average");
        double medianFps = fps != null ? fps.optDouble("median", 0.0) : fpsFromMs(ms, "median");
        double minFps = fps != null ? fps.optDouble("min", 0.0) : inverseMs(ms, "max");
        double maxFps = fps != null ? fps.optDouble("max", 0.0) : inverseMs(ms, "min");
        return new BenchmarkResult(
                run,
                averageFps,
                medianFps,
                minFps,
                maxFps,
                json.optInt("frameCount", 0),
                json.optDouble("measuredDurationSeconds", 0.0));
    }

    private double fpsFromMs(JSONObject ms, String key) {
        return inverseMs(ms, key);
    }

    private double inverseMs(JSONObject ms, String key) {
        if (ms == null) {
            return 0.0;
        }
        double value = ms.optDouble(key, 0.0);
        return value > 0.0 ? 1000.0 / value : 0.0;
    }

    private String buildBenchmarkReport() {
        double max = 1.0;
        for (BenchmarkResult result : benchmarkResults) {
            if (result.averageFps > max) {
                max = result.averageFps;
            }
        }

        StringBuilder out = new StringBuilder();
        out.append("DayScene Vulkan Benchmark\n\n");
        for (BenchmarkResult result : benchmarkResults) {
            int bars = (int)Math.max(1, Math.round((result.averageFps / max) * 28.0));
            out.append(String.format(Locale.US,
                    "%-9s %-9s avg %7.2f fps med %7.2f min %7.2f max %7.2f %s\n",
                    result.run.resolution(),
                    result.run.mode(),
                    result.averageFps,
                    result.medianFps,
                    result.minFps,
                    result.maxFps,
                    repeat('#', bars)));
        }
        return out.toString();
    }

    private String repeat(char value, int count) {
        StringBuilder out = new StringBuilder(count);
        for (int i = 0; i < count; ++i) {
            out.append(value);
        }
        return out.toString();
    }

    private String readTextFile(File file) throws IOException {
        FileInputStream stream = new FileInputStream(file);
        try {
            byte[] data = new byte[(int) file.length()];
            int offset = 0;
            while (offset < data.length) {
                int read = stream.read(data, offset, data.length - offset);
                if (read < 0) {
                    break;
                }
                offset += read;
            }
            return new String(data, 0, offset, "UTF-8");
        } finally {
            stream.close();
        }
    }

    private AssetOption selectedSceneFile() {
        Object selected = sceneFileSpinner.getSelectedItem();
        if (selected instanceof AssetOption) {
            return (AssetOption) selected;
        }
        return new AssetOption(defaultSceneFilePath(), "Scenes/");
    }

    private AssetOption selectedModel() {
        Object selected = modelSpinner.getSelectedItem();
        if (selected instanceof AssetOption) {
            return (AssetOption) selected;
        }
        return new AssetOption(defaultModelPath(), "Models/");
    }

    private void loadSceneFiles() {
        List<String> paths = new ArrayList<>();
        collectAssets(getAssets(), "Scenes", paths, true);
        Collections.sort(paths, String.CASE_INSENSITIVE_ORDER);
        if (paths.isEmpty()) {
            Toast.makeText(this, "No packaged .t8scene files found; using Q3 default.", Toast.LENGTH_LONG).show();
            paths.add("Scenes/Q3/q3dm6_mod_3.t8scene");
        }
        for (String path : paths) {
            sceneFiles.add(new AssetOption(path, "Scenes/"));
        }
    }

    private void loadModels() {
        List<String> paths = new ArrayList<>();
        collectAssets(getAssets(), "Models", paths, false);
        collectDiskModels(paths);
        Collections.sort(paths, String.CASE_INSENSITIVE_ORDER);
        if (paths.isEmpty()) {
            Toast.makeText(this, "No packaged models found; using DamagedHelmet default.", Toast.LENGTH_LONG).show();
            paths.add("Models/DamagedHelmet.glb");
        }
        for (String path : paths) {
            models.add(new AssetOption(path, "Models/"));
        }
    }

    private void collectDiskModels(List<String> out) {
        HashSet<String> known = new HashSet<>(out);
        File root = getExternalFilesDir(null);
        if (root != null) {
            collectDiskModelsRecursive(new File(root, "Models"), "Models", out, known);
        }
        collectDiskModelsRecursive(new File(getFilesDir(), "Models"), "Models", out, known);
    }

    private void collectDiskModelsRecursive(File directory, String resourceDirectory, List<String> out, HashSet<String> known) {
        File[] entries = directory.listFiles();
        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            String resourcePath = resourceDirectory + "/" + entry.getName();
            if (entry.isDirectory()) {
                collectDiskModelsRecursive(entry, resourcePath, out, known);
            } else if (isModel(resourcePath) && known.add(resourcePath)) {
                out.add(resourcePath);
            }
        }
    }
    private void collectAssets(AssetManager assets, String directory, List<String> out, boolean sceneFileMode) {
        String[] entries;
        try {
            entries = assets.list(directory);
        } catch (IOException e) {
            return;
        }
        if (entries == null) {
            return;
        }
        for (String entry : entries) {
            String path = directory + "/" + entry;
            if (sceneFileMode ? isSceneFile(path) : isModel(path)) {
                out.add(path);
                continue;
            }
            collectAssets(assets, path, out, sceneFileMode);
        }
    }

    private boolean isSceneFile(String path) {
        return path.toLowerCase(Locale.ROOT).endsWith(".t8scene");
    }

    private boolean isModel(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        return lower.endsWith(".glb") || lower.endsWith(".gltf");
    }

    private String defaultSceneFilePath() {
        return defaultSceneFilePath(selectedScene().value);
    }

    private String defaultSceneFilePath(int scene) {
        String preferred = scene == 4
                ? "Scenes/Q3/q3dm6_mod_3_jolt.t8scene"
                : "Scenes/Q3/q3dm6_mod_3.t8scene";
        for (AssetOption sceneFile : sceneFiles) {
            if (sceneFile.path.equals(preferred)) {
                return sceneFile.path;
            }
        }
        return sceneFiles.isEmpty() ? preferred : sceneFiles.get(0).path;
    }

    private String defaultModelPath() {
        return models.isEmpty() ? "Models/DamagedHelmet.glb" : models.get(0).path;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
