package com.t850.engine;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public final class AssetDownloadActivity extends Activity {
    private static final String TAG = "T850AssetDownload";
    private static final String RUNTIME_MODEL_MANIFEST_URL = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json";
    private static final String TEXTURE_MANIFEST_URL = "https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json";
    private static final int WORKER_COUNT = 7;

    private ProgressBar progressBar;
    private TextView statusText;
    private ExecutorService executor;
    private volatile boolean destroyed;

    private static final class AssetEntry {
        final String resourcePath;
        final String url;
        final long size;

        AssetEntry(String resourcePath, String url, long size) {
            this.resourcePath = resourcePath;
            this.url = url;
            this.size = size;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        executor = Executors.newFixedThreadPool(WORKER_COUNT);
        Thread coordinator = new Thread(this::checkAndDownloadAssets, "T850AssetCoordinator");
        coordinator.start();
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        if (executor != null) {
            executor.shutdownNow();
        }
        super.onDestroy();
    }

    private void buildUi() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        ImageView logo = new ImageView(this);
        logo.setImageResource(getResources().getIdentifier("logo", "drawable", getPackageName()));
        logo.setAdjustViewBounds(true);
        logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
        FrameLayout.LayoutParams logoParams = new FrameLayout.LayoutParams(dp(360), dp(180));
        logoParams.gravity = Gravity.CENTER;
        root.addView(logo, logoParams);

        LinearLayout bottom = new LinearLayout(this);
        bottom.setOrientation(LinearLayout.VERTICAL);
        bottom.setPadding(dp(32), 0, dp(32), dp(28));
        FrameLayout.LayoutParams bottomParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT);
        bottomParams.gravity = Gravity.BOTTOM;

        statusText = new TextView(this);
        statusText.setTextColor(Color.WHITE);
        statusText.setTextSize(14.0f);
        statusText.setGravity(Gravity.CENTER);
        statusText.setText("Checking game data...");
        bottom.addView(statusText, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setProgress(0);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(10));
        progressParams.topMargin = dp(10);
        bottom.addView(progressBar, progressParams);

        root.addView(bottom, bottomParams);
        setContentView(root);
    }

    private void checkAndDownloadAssets() {
        try {
            updateProgress(0, 0, 1, "Checking game data...");
            List<AssetEntry> assets = new ArrayList<>();
            assets.addAll(fetchManifest(RUNTIME_MODEL_MANIFEST_URL, "Models", null));
            assets.addAll(fetchManifest(TEXTURE_MANIFEST_URL, "Textures", "texture"));
            if (assets.isEmpty()) {
                throw new IllegalStateException("Manifests did not contain runtime assets.");
            }

            File assetRoot = getExternalFilesDir(null);
            if (assetRoot == null) {
                assetRoot = getFilesDir();
            }
            final File finalAssetRoot = assetRoot;
            AtomicInteger completed = new AtomicInteger(0);
            AtomicInteger failed = new AtomicInteger(0);
            int total = assets.size();
            updateProgress(0, 0, total, "Checking 0/" + total + " assets...");

            for (AssetEntry asset : assets) {
                executor.submit(() -> {
                    try {
                        File target = new File(finalAssetRoot, asset.resourcePath.replace('/', File.separatorChar));
                        if (!isAssetReady(target, asset.size)) {
                            downloadAsset(asset, target);
                        }
                    } catch (Exception ex) {
                        Log.e(TAG, "Failed to prepare " + asset.resourcePath, ex);
                        failed.incrementAndGet();
                    } finally {
                        int done = completed.incrementAndGet();
                        int failCount = failed.get();
                        String status = failCount == 0
                                ? String.format(Locale.ROOT, "Downloading game data %d/%d", done, total)
                                : String.format(Locale.ROOT, "Downloading game data %d/%d (%d failed)", done, total, failCount);
                        updateProgress(done, done, total, status);
                    }
                });
            }

            while (!destroyed && completed.get() < total) {
                Thread.sleep(100);
            }

            if (destroyed) {
                return;
            }
            if (failed.get() > 0) {
                updateStatus("Download failed. Check connection and restart T8 to retry.");
                return;
            }

            updateProgress(total, total, total, "Game data ready.");
            Thread.sleep(250);
            runOnUiThread(this::startLauncher);
        } catch (Exception ex) {
            Log.e(TAG, "Asset download setup failed", ex);
            updateStatus("Download setup failed: " + ex.getMessage());
        }
    }

    private List<AssetEntry> fetchManifest(String manifestUrl, String defaultRoot, String requiredKind) throws Exception {
        String json = readUrl(manifestUrl);
        JSONObject root = new JSONObject(json);
        JSONArray assets = root.getJSONArray("assets");
        List<AssetEntry> entries = new ArrayList<>();
        for (int i = 0; i < assets.length(); ++i) {
            JSONObject item = assets.getJSONObject(i);
            String key = item.optString("localRelativePath", "");
            if (key.isEmpty()) {
                key = item.optString("key", "");
            }
            if (key.isEmpty()) {
                key = item.optString("localPath", "");
            }
            String url = item.optString("url", "");
            if (key.isEmpty() && !url.isEmpty()) {
                int slash = url.lastIndexOf('/');
                key = slash >= 0 ? url.substring(slash + 1) : url;
            }
            if (url.isEmpty() || key.isEmpty()) {
                continue;
            }
            String rawPath = normalizeManifestPath(key);
            String kind = item.optString("kind", "");
            if (requiredKind != null
                    && !kind.equalsIgnoreCase(requiredKind)
                    && !rawPath.toLowerCase(Locale.ROOT).startsWith(defaultRoot.toLowerCase(Locale.ROOT) + "/")) {
                continue;
            }
            String resourcePath = normalizeAssetPath(rawPath, defaultRoot);
            long size = item.has("size") ? item.optLong("size", 0L) : 0L;
            entries.add(new AssetEntry(resourcePath, url, size));
        }
        return entries;
    }

    private static String normalizeManifestPath(String key) {
        String path = key.replace('\\', '/');
        while (path.startsWith("/")) {
            path = path.substring(1);
        }
        if (path.startsWith("Assets/")) {
            path = path.substring("Assets/".length());
        }
        int assetsIndex = path.indexOf("/Assets/");
        if (assetsIndex >= 0) {
            path = path.substring(assetsIndex + "/Assets/".length());
        }
        return path;
    }

    private static String normalizeAssetPath(String path, String defaultRoot) {
        if (!path.startsWith(defaultRoot + "/")) {
            path = defaultRoot + "/" + path;
        }
        return path;
    }

    private static boolean isAssetReady(File target, long expectedSize) {
        if (!target.isFile()) {
            return false;
        }
        long length = target.length();
        if (expectedSize > 0L) {
            return length == expectedSize;
        }
        return length > 0L;
    }

    private static String readUrl(String urlText) throws Exception {
        HttpURLConnection connection = (HttpURLConnection)new URL(urlText).openConnection();
        connection.setConnectTimeout(10000);
        connection.setReadTimeout(30000);
        connection.setUseCaches(false);
        int status = connection.getResponseCode();
        if (status < 200 || status >= 300) {
            connection.disconnect();
            throw new IllegalStateException("HTTP " + status + " for manifest");
        }
        try (InputStream input = new BufferedInputStream(connection.getInputStream())) {
            byte[] buffer = new byte[64 * 1024];
            StringBuilder builder = new StringBuilder();
            int read;
            while ((read = input.read(buffer)) >= 0) {
                builder.append(new String(buffer, 0, read, java.nio.charset.StandardCharsets.UTF_8));
            }
            return builder.toString();
        } finally {
            connection.disconnect();
        }
    }

    private static void downloadAsset(AssetEntry asset, File target) throws Exception {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException("Could not create " + parent);
        }
        File tmp = new File(target.getAbsolutePath() + ".download");
        if (tmp.exists() && !tmp.delete()) {
            throw new IllegalStateException("Could not remove old partial download " + tmp);
        }

        HttpURLConnection connection = (HttpURLConnection)new URL(asset.url).openConnection();
        connection.setConnectTimeout(10000);
        connection.setReadTimeout(60000);
        connection.setUseCaches(false);
        int status = connection.getResponseCode();
        if (status < 200 || status >= 300) {
            connection.disconnect();
            throw new IllegalStateException("HTTP " + status + " for " + asset.resourcePath);
        }
        try (InputStream input = new BufferedInputStream(connection.getInputStream());
             FileOutputStream output = new FileOutputStream(tmp)) {
            byte[] buffer = new byte[256 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                output.write(buffer, 0, read);
            }
        } finally {
            connection.disconnect();
        }

        if (asset.size > 0L && tmp.length() != asset.size) {
            tmp.delete();
            throw new IllegalStateException("Size mismatch for " + asset.resourcePath);
        }
        if (!tmp.renameTo(target)) {
            tmp.delete();
            throw new IllegalStateException("Could not finalize " + asset.resourcePath);
        }
    }

    private void updateProgress(int progressValue, int completed, int total, String status) {
        int percent = total > 0 ? Math.min(100, Math.max(0, (int)Math.floor((completed * 100.0f) / total))) : progressValue;
        runOnUiThread(() -> {
            progressBar.setProgress(percent);
            statusText.setText(status + "  " + percent + "%");
        });
        Log.i(TAG, status + " " + percent + "%");
    }

    private void updateStatus(String status) {
        runOnUiThread(() -> statusText.setText(status));
    }

    private void startLauncher() {
        if (destroyed) {
            return;
        }
        Intent intent = new Intent(this, LauncherActivity.class);
        Bundle extras = getIntent().getExtras();
        if (extras != null) {
            intent.putExtras(extras);
        }
        startActivity(intent);
        finish();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}