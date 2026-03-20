package io.github.banjorecomp.android;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class BanjoRecompiledActivity extends SDLActivity {
    private static final int REQUEST_CODE_OPEN_DOCUMENT = 0x424b;

    private boolean pendingAllowMultipleSelection = false;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "BanjoRecompiled"
        };
    }

    public void openDocumentPicker(final boolean allowMultiple) {
        pendingAllowMultipleSelection = allowMultiple;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, allowMultiple);
            intent.putExtra(Intent.EXTRA_LOCAL_ONLY, true);

            try {
                startActivityForResult(intent, REQUEST_CODE_OPEN_DOCUMENT);
            } catch (ActivityNotFoundException exception) {
                nativeOnDocumentPickerResult(null, "No document provider was available to choose a file.");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != REQUEST_CODE_OPEN_DOCUMENT) {
            return;
        }

        if (resultCode != RESULT_OK || data == null) {
            nativeOnDocumentPickerResult(null, null);
            return;
        }

        try {
            List<Uri> uris = collectSelectedUris(data, pendingAllowMultipleSelection);
            if (uris.isEmpty()) {
                nativeOnDocumentPickerResult(null, null);
                return;
            }

            File importDirectory = new File(getCacheDir(), "document-imports");
            recreateDirectory(importDirectory);

            ArrayList<String> copiedPaths = new ArrayList<>();
            for (int i = 0; i < uris.size(); i++) {
                Uri uri = uris.get(i);
                copiedPaths.add(copyUriToImportFile(uri, importDirectory, i).getAbsolutePath());
            }

            nativeOnDocumentPickerResult(copiedPaths.toArray(new String[0]), null);
        } catch (IOException exception) {
            nativeOnDocumentPickerResult(null, exception.getMessage());
        }
    }

    private List<Uri> collectSelectedUris(Intent data, boolean allowMultiple) {
        ArrayList<Uri> uris = new ArrayList<>();

        ClipData clipData = data.getClipData();
        if (allowMultiple && clipData != null) {
            for (int i = 0; i < clipData.getItemCount(); i++) {
                Uri uri = clipData.getItemAt(i).getUri();
                if (uri != null) {
                    uris.add(uri);
                }
            }
        } else {
            Uri uri = data.getData();
            if (uri != null) {
                uris.add(uri);
            }
        }

        return uris;
    }

    private File copyUriToImportFile(Uri uri, File importDirectory, int index) throws IOException {
        final int takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, takeFlags);
        } catch (SecurityException ignored) {
        }

        String displayName = queryDisplayName(uri);
        if (displayName == null || displayName.isEmpty()) {
            displayName = String.format(Locale.ROOT, "import-%d.bin", index);
        }

        File outputFile = new File(importDirectory, String.format(Locale.ROOT, "%02d-%s", index, sanitizeFilename(displayName)));
        try (InputStream inputStream = getContentResolver().openInputStream(uri);
             OutputStream outputStream = new FileOutputStream(outputFile)) {
            if (inputStream == null) {
                throw new IOException("Failed to open the selected document.");
            }

            byte[] buffer = new byte[16 * 1024];
            int bytesRead;
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }
        }

        return outputFile;
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameColumn = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (nameColumn >= 0) {
                    return cursor.getString(nameColumn);
                }
            }
        }

        return null;
    }

    private static String sanitizeFilename(String filename) {
        String sanitized = filename.replaceAll("[^A-Za-z0-9._-]", "_");
        if (sanitized.isEmpty()) {
            return "import.bin";
        }
        return sanitized;
    }

    private static void recreateDirectory(File directory) throws IOException {
        deleteRecursively(directory);
        if (!directory.mkdirs() && !directory.isDirectory()) {
            throw new IOException("Failed to prepare the app import directory.");
        }
    }

    private static void deleteRecursively(File file) throws IOException {
        if (!file.exists()) {
            return;
        }

        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }

        if (!file.delete()) {
            throw new IOException("Failed to clear old imported files.");
        }
    }

    private static native void nativeOnDocumentPickerResult(String[] copiedPaths, String errorMessage);
}
