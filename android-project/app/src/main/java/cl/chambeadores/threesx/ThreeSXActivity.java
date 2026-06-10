package cl.chambeadores.threesx;

import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * 3SX Android entry point.
 *
 * For v1, the ROM file (SF33RD.AFS) must be pre-staged into the app's
 * internal storage at `<filesDir>/resources/SF33RD.AFS` before launch.
 * Use `install-and-run.bat` from a PC with USB debugging — it pushes the
 * AFS into the sandbox via adb run-as.
 *
 * If the ROM is missing, the activity shows a toast and exits. (Future
 * v2 work: bootstrap activity with SAF picker — separated from SDLActivity
 * so it doesn't fight SDLActivity's recreate() flow.)
 */
public class ThreeSXActivity extends SDLActivity {

    private static final String TAG = "3SX";
    private static final String ROM_REL_PATH = "resources/SF33RD.AFS";

    @Override
    protected String[] getLibraries() {
        // lib3sx.so is built by CMake externalNativeBuild from the project
        // root CMakeLists.txt. SDL3 and its deps come from the cross-compiled
        // .so files placed in app/src/main/jniLibs/arm64-v8a/.
        return new String[]{ "SDL3", "3sx" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File rom = new File(getFilesDir(), ROM_REL_PATH);
        if (!rom.exists() || rom.length() == 0) {
            Log.e(TAG, "ROM missing or empty at " + rom.getAbsolutePath()
                    + " (size=" + (rom.exists() ? rom.length() : -1) + ")");
            Toast.makeText(this,
                    "SF33RD.AFS missing. Use install-and-run.bat to stage it.",
                    Toast.LENGTH_LONG).show();
            super.onCreate(savedInstanceState);
            finish();
            return;
        }
        Log.i(TAG, "ROM present: " + rom.getAbsolutePath() + " size=" + rom.length());
        super.onCreate(savedInstanceState);
    }
}
