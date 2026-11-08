# Photo Circle Animator

Photo Circle Animator is a small C++ Windows desktop app that loads a photo, detects a dominant high-contrast shape, and animates rotating Fourier circles whose endpoint traces that outline.

The photo stays hidden while the circle outlines (slightly transparent white), connecting lines, and trace animate on a black background. The circle count starts at one and grows by about 50% after each completed loop, up to the **Max** value (1–512) selected in the window. Animation speed also increases by 8% per loop, up to 3× its initial speed. When the selected maximum is reached, the animation pauses automatically and shows the photo. The 98% score measures how many of 1024 evenly spaced outline samples are within 1.8% of the analysis-image diagonal (at least 2 pixels) of the approximation. Circle outlines and arms are batched during rendering to reduce lag at high counts. Each old outline is cleared when the circle count increases; completed traces remain visible during the transformation. Rendering uses an off-screen buffer to reduce flicker.

## Easy start (no terminal commands)

Double-click **Start Photo Circle Animator.vbs** to build the latest source and open the app. The launcher always rebuilds before starting, so it cannot silently open an older version; close any running copy first so Windows can replace its executable. Do not launch `PhotoCircleAnimator.exe` directly, since it may be an outdated build. The launcher hides the build console and shows a helpful message if building fails. Build details are saved to `PhotoCircleAnimator-build.log`.

The first launch needs the **MSYS2 UCRT64 MinGW-w64 C++ compiler** installed. The launcher looks for it in `C:\msys64\ucrt64\bin` or on your `PATH`. If you already have that compiler and the app still does not start, double-click **Start Photo Circle Animator.bat** to see the build output.

## Build

Requirements for the manual CMake build: Windows, CMake, and a C++17 compiler with MinGW-w64 Windows/GDI+ headers and libraries.

From PowerShell:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Or compile directly with MinGW-w64:

```powershell
g++ -std=c++17 -O2 -mwindows main.cpp -lgdiplus -lcomdlg32 -lgdi32 -luser32 -o PhotoCircleAnimator.exe
```

Run `PhotoCircleAnimator.exe`, choose **Open photo...**, set **Max circles** to the desired limit (1–512), then click **Apply** or press Enter. The large counter and status use that selected limit as the animation ramps up. **Pause**/**Play** controls the transformation; it pauses automatically at the selected maximum with the photo shown. Press **Play** after completion to restart from one circle.

## Outline extraction

The app downsamples the photo for analysis, applies Otsu's grayscale threshold, and uses the largest non-border connected region as the foreground candidate. It traces and resamples that region's boundary before calculating and ranking Fourier coefficients. This works best for a clear, high-contrast subject against a simpler background; it does not perform general-purpose semantic object segmentation.
