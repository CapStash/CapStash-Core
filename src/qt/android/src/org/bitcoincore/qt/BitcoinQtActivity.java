package org.CapStashcore.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class CapStashQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File CapStashDir = new File(getFilesDir().getAbsolutePath() + "/.CapStash");
        if (!CapStashDir.exists()) {
            CapStashDir.mkdir();
        }

        super.onCreate(savedInstanceState);
    }
}
