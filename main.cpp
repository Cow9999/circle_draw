#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iterator>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
namespace {

constexpr int kOpenButton = 1001;
constexpr int kPlayButton = 1002;
constexpr int kStatusLabel = 1003;
constexpr int kCircleCountLabel = 1004;
constexpr int kMaximumLabel = 1005;
constexpr int kMaximumEdit = 1006;
constexpr int kApplyMaximumButton = 1007;
constexpr int kAnalysisSide = 384;
constexpr int kSampleCount = 1024;
constexpr int kMaximumCircles = 512;
constexpr int kInitialCircleCount = 1;
constexpr int kSimilarityTarget = 98;
constexpr std::size_t kMaximumSavedTraces = 12;
constexpr UINT_PTR kAnimationTimer = 1;
constexpr double kPi = 3.14159265358979323846;
constexpr double kNormalPhaseStep = 1.0 / 360.0;
constexpr double kAnimationSpeedIncrease = 1.08;
constexpr double kMaximumAnimationSpeedMultiplier = 3.0;
constexpr int kMaximumSpeedRampLoops = 15;

using Complex = std::complex<double>;

struct FourierTerm {
    int frequency = 0;
    Complex coefficient{};
};

struct AppState {
    HWND window = nullptr;
    HWND openButton = nullptr;
    HWND playButton = nullptr;
    HWND statusLabel = nullptr;
    HWND circleCountLabel = nullptr;
    HWND maximumLabel = nullptr;
    HWND maximumEdit = nullptr;
    HWND applyMaximumButton = nullptr;
    WNDPROC maximumEditPreviousProcedure = nullptr;
    Gdiplus::Bitmap* image = nullptr;
    std::wstring imagePath;
    std::vector<unsigned char> grayscale;
    int analysisWidth = 0;
    int analysisHeight = 0;
    int threshold = 128;
    int maximumCircles = kMaximumCircles;
    std::vector<Complex> contour;
    std::vector<FourierTerm> terms;
    int circleCount = 0;
    int targetCircleCount = 0;
    int similarity = 0;
    int maximumSimilarity = 0;
    bool targetAvailable = false;
    bool targetReached = false;
    bool animationFinished = false;
    bool playing = true;
    int completedLoops = 0;
    double phase = 0.0;
    std::vector<Gdiplus::PointF> trace;
    std::vector<std::vector<Gdiplus::PointF>> completedTraces;
};

AppState g_app;
HBRUSH g_blackBrush = nullptr;

struct Component {
    std::vector<int> pixels;
    int area = 0;
    bool touchesBorder = true;
};

int calculateOtsuThreshold(const std::vector<unsigned char>& pixels) {
    if (pixels.empty()) {
        return 128;
    }

    std::uint64_t histogram[256] = {};
    for (unsigned char pixel : pixels) {
        ++histogram[pixel];
    }

    const std::uint64_t total = pixels.size();
    long double weightedTotal = 0.0L;
    for (int value = 0; value < 256; ++value) {
        weightedTotal += static_cast<long double>(value) * histogram[value];
    }

    std::uint64_t backgroundWeight = 0;
    long double backgroundTotal = 0.0L;
    long double bestVariance = -1.0L;
    int bestThreshold = 128;
    for (int value = 0; value < 256; ++value) {
        backgroundWeight += histogram[value];
        if (backgroundWeight == 0) {
            continue;
        }

        const std::uint64_t foregroundWeight = total - backgroundWeight;
        if (foregroundWeight == 0) {
            break;
        }

        backgroundTotal += static_cast<long double>(value) * histogram[value];
        const long double backgroundMean =
            backgroundTotal / static_cast<long double>(backgroundWeight);
        const long double foregroundMean =
            (weightedTotal - backgroundTotal) / static_cast<long double>(foregroundWeight);
        const long double difference = backgroundMean - foregroundMean;
        const long double variance =
            static_cast<long double>(backgroundWeight) *
            static_cast<long double>(foregroundWeight) * difference * difference;
        if (variance > bestVariance) {
            bestVariance = variance;
            bestThreshold = value;
        }
    }

    return std::clamp(bestThreshold, 10, 245);
}

Component findLargestComponent(const std::vector<unsigned char>& mask, int width, int height) {
    Component largest;
    Component largestInterior;
    std::vector<unsigned char> visited(mask.size(), 0);
    std::queue<int> pending;

    constexpr int dx[4] = {-1, 1, 0, 0};
    constexpr int dy[4] = {0, 0, -1, 1};

    for (int start = 0; start < static_cast<int>(mask.size()); ++start) {
        if (mask[start] == 0 || visited[start] != 0) {
            continue;
        }

        Component current;
        bool touchesBorder = false;
        visited[start] = 1;
        pending.push(start);
        while (!pending.empty()) {
            const int index = pending.front();
            pending.pop();
            current.pixels.push_back(index);
            const int x = index % width;
            const int y = index / width;
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                touchesBorder = true;
            }

            for (int direction = 0; direction < 4; ++direction) {
                const int nextX = x + dx[direction];
                const int nextY = y + dy[direction];
                if (nextX < 0 || nextY < 0 || nextX >= width || nextY >= height) {
                    continue;
                }
                const int next = nextY * width + nextX;
                if (mask[next] != 0 && visited[next] == 0) {
                    visited[next] = 1;
                    pending.push(next);
                }
            }
        }

        current.area = static_cast<int>(current.pixels.size());
        current.touchesBorder = touchesBorder;
        if (current.area > largest.area) {
            largest = current;
        }
        if (!touchesBorder && current.area > largestInterior.area) {
            largestInterior = std::move(current);
        }
    }

    return largestInterior.area > 0 ? std::move(largestInterior) : std::move(largest);
}

std::vector<Complex> traceBoundary(const Component& component, int width, int height) {
    if (component.pixels.size() < 8) {
        return {};
    }

    std::vector<unsigned char> mask(static_cast<std::size_t>(width) * height, 0);
    int start = component.pixels.front();
    for (int pixel : component.pixels) {
        mask[pixel] = 1;
        const int x = pixel % width;
        const int y = pixel / width;
        const int startX = start % width;
        const int startY = start / width;
        if (y < startY || (y == startY && x < startX)) {
            start = pixel;
        }
    }

    constexpr int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    constexpr int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

    int current = start;
    int backtrackX = start % width - 1;
    int backtrackY = start / width;
    int firstNext = -1;
    std::vector<Complex> boundary;
    const std::size_t maximumSteps = component.pixels.size() * 8 + 8;

    for (std::size_t step = 0; step < maximumSteps; ++step) {
        const int x = current % width;
        const int y = current / width;
        boundary.emplace_back(static_cast<double>(x), static_cast<double>(y));

        int backtrackDirection = 7;
        const int offsetX = backtrackX - x;
        const int offsetY = backtrackY - y;
        for (int direction = 0; direction < 8; ++direction) {
            if (dx[direction] == offsetX && dy[direction] == offsetY) {
                backtrackDirection = direction;
                break;
            }
        }

        int next = -1;
        int foundDirection = -1;
        for (int scan = 1; scan <= 8; ++scan) {
            const int direction = (backtrackDirection + scan) % 8;
            const int nextX = x + dx[direction];
            const int nextY = y + dy[direction];
            if (nextX >= 0 && nextY >= 0 && nextX < width && nextY < height &&
                mask[nextY * width + nextX] != 0) {
                next = nextY * width + nextX;
                foundDirection = direction;
                break;
            }
        }

        if (next < 0) {
            break;
        }
        if (firstNext < 0) {
            firstNext = next;
        } else if (current == start && next == firstNext) {
            boundary.pop_back();
            break;
        }

        const int previousDirection = (foundDirection + 7) % 8;
        backtrackX = x + dx[previousDirection];
        backtrackY = y + dy[previousDirection];
        current = next;
    }

    if (boundary.size() < 8) {
        return {};
    }
    return boundary;
}

std::vector<Complex> resampleClosedContour(
    const std::vector<Complex>& boundary,
    int width,
    int height) {
    if (boundary.size() < 2) {
        return {};
    }

    std::vector<double> cumulative(boundary.size() + 1, 0.0);
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const Complex difference = boundary[(i + 1) % boundary.size()] - boundary[i];
        cumulative[i + 1] = cumulative[i] + std::abs(difference);
    }
    const double perimeter = cumulative.back();
    if (perimeter <= 0.0) {
        return {};
    }

    std::vector<Complex> samples;
    samples.reserve(kSampleCount);
    std::size_t segment = 0;
    for (int sample = 0; sample < kSampleCount; ++sample) {
        const double targetDistance = perimeter * sample / kSampleCount;
        while (segment + 1 < cumulative.size() &&
               cumulative[segment + 1] < targetDistance) {
            ++segment;
        }

        const std::size_t first = segment % boundary.size();
        const std::size_t second = (first + 1) % boundary.size();
        const double length = cumulative[segment + 1] - cumulative[segment];
        const double fraction = length > 0.0
            ? (targetDistance - cumulative[segment]) / length
            : 0.0;
        const Complex point = boundary[first] +
            (boundary[second] - boundary[first]) * fraction;
        samples.emplace_back(point.real() - width / 2.0, point.imag() - height / 2.0);
    }
    return samples;
}

std::vector<FourierTerm> calculateFourierTerms(const std::vector<Complex>& samples) {
    std::vector<FourierTerm> terms;
    const int count = static_cast<int>(samples.size());
    if (count == 0) {
        return terms;
    }

    terms.reserve(count);
    for (int frequency = -count / 2; frequency < count / 2; ++frequency) {
        Complex coefficient{};
        for (int sample = 0; sample < count; ++sample) {
            const double angle = -2.0 * kPi * frequency * sample / count;
            coefficient += samples[sample] * Complex(std::cos(angle), std::sin(angle));
        }
        coefficient /= static_cast<double>(count);
        terms.push_back({frequency, coefficient});
    }

    std::stable_sort(terms.begin(), terms.end(), [](const FourierTerm& left, const FourierTerm& right) {
        return std::abs(left.coefficient) > std::abs(right.coefficient);
    });
    if (terms.size() > kMaximumCircles) {
        terms.resize(kMaximumCircles);
    }
    return terms;
}

Complex evaluateTerms(const std::vector<FourierTerm>& terms, int count, double phase) {
    Complex point{};
    const double angleScale = 2.0 * kPi * phase;
    const int used = std::min(count, static_cast<int>(terms.size()));
    for (int index = 0; index < used; ++index) {
        const FourierTerm& term = terms[index];
        const double angle = angleScale * term.frequency;
        point += term.coefficient * Complex(std::cos(angle), std::sin(angle));
    }
    return point;
}

int maximumCircleCount(const AppState& app) {
    return std::min(app.maximumCircles, static_cast<int>(app.terms.size()));
}

int nextCircleCount(int currentCount, int maximum) {
    const int nextCount = std::max(currentCount + 1,
        static_cast<int>(std::ceil(currentCount * 1.5)));
    return std::min(maximum, nextCount);
}

int measureSimilarity(const AppState& app, int circleCount) {
    if (app.contour.empty() || app.terms.empty()) {
        return 0;
    }

    const double diagonal = std::hypot(app.analysisWidth, app.analysisHeight);
    const double tolerance = std::max(2.0, diagonal * 0.018);
    const int pointCount = static_cast<int>(app.contour.size());
    int matched = 0;
    for (int sample = 0; sample < pointCount; ++sample) {
        const Complex approximation = evaluateTerms(app.terms, circleCount,
            static_cast<double>(sample) / pointCount);
        if (std::abs(approximation - app.contour[sample]) <= tolerance) {
            ++matched;
        }
    }
    return matched * 100 / pointCount;
}

void updateApproximationStats(AppState& app) {
    const int maximum = maximumCircleCount(app);
    app.targetCircleCount = maximum;
    app.similarity = 0;
    app.maximumSimilarity = 0;
    app.targetAvailable = false;
    app.targetReached = false;
    if (app.contour.empty() || app.terms.empty()) {
        return;
    }

    int bestScore = -1;
    for (int count = kInitialCircleCount;; count = nextCircleCount(count, maximum)) {
        const int score = measureSimilarity(app, count);
        bestScore = std::max(bestScore, score);
        if (score >= kSimilarityTarget) {
            app.targetCircleCount = count;
            app.targetAvailable = true;
            break;
        }
        if (count == maximum) {
            break;
        }
    }
    app.maximumSimilarity = std::max(0, bestScore);
    app.similarity = measureSimilarity(app, app.circleCount);
    app.targetReached = app.similarity >= kSimilarityTarget;
}

void findBestApproximation(AppState& app) {
    app.circleCount = app.terms.empty() ? 0 : kInitialCircleCount;
    updateApproximationStats(app);
}

void updateCircleCountDisplay(AppState& app) {
    if (app.circleCountLabel == nullptr) {
        return;
    }
    const std::wstring text = L"Circles: " + std::to_wstring(app.circleCount) +
        L" / " + std::to_wstring(app.maximumCircles);
    SetWindowTextW(app.circleCountLabel, text.c_str());
}

void updateStatus(AppState& app) {
    updateCircleCountDisplay(app);
    if (app.statusLabel == nullptr) {
        return;
    }

    std::wstring text;
    if (app.image == nullptr) {
        text = L"Open a photo to begin.";
    } else if (app.contour.empty()) {
        text = L"No clear outline found. Choose a higher-contrast photo.";
    } else if (app.animationFinished) {
        text = L"Reached " + std::to_wstring(app.circleCount) +
            L" circles; paused with the photo shown.";
    } else if (app.circleCount >= maximumCircleCount(app)) {
        text = L"At max of " + std::to_wstring(maximumCircleCount(app)) + L" circles.";
    } else if (app.targetReached) {
        text = L"98% target reached at " + std::to_wstring(app.targetCircleCount) +
            L" circles; continuing up to " +
            std::to_wstring(maximumCircleCount(app)) + L".";
    } else if (app.targetAvailable) {
        text = L"Match: " + std::to_wstring(app.similarity) +
            L"% | circles: " + std::to_wstring(app.circleCount) + L"/" +
            std::to_wstring(maximumCircleCount(app)) + L" (98% target at " +
            std::to_wstring(app.targetCircleCount) + L").";
    } else {
        text = L"Best at max: " + std::to_wstring(app.maximumSimilarity) +
            L"% | circles: " + std::to_wstring(app.circleCount) + L"/" +
            std::to_wstring(maximumCircleCount(app)) + L".";
    }
    SetWindowTextW(app.statusLabel, text.c_str());
}

bool buildOutline(AppState& app, bool inverted) {
    app.contour.clear();
    app.terms.clear();
    app.trace.clear();
    app.completedTraces.clear();
    app.circleCount = 0;
    app.completedLoops = 0;
    app.animationFinished = false;
    app.phase = 0.0;
    InvalidateRect(app.window, nullptr, FALSE);
    if (app.grayscale.empty() || app.analysisWidth <= 0 || app.analysisHeight <= 0) {
        updateStatus(app);
        return false;
    }

    std::vector<unsigned char> darkMask(app.grayscale.size());
    std::vector<unsigned char> lightMask(app.grayscale.size());
    for (std::size_t index = 0; index < app.grayscale.size(); ++index) {
        darkMask[index] = app.grayscale[index] <= app.threshold ? 1 : 0;
        lightMask[index] = app.grayscale[index] > app.threshold ? 1 : 0;
    }

    Component dark = findLargestComponent(darkMask, app.analysisWidth, app.analysisHeight);
    Component light = findLargestComponent(lightMask, app.analysisWidth, app.analysisHeight);
    const bool darkIsPreferred =
        dark.touchesBorder != light.touchesBorder
            ? !dark.touchesBorder
            : dark.area >= light.area;
    const bool chooseDark = inverted ? !darkIsPreferred : darkIsPreferred;
    Component selected = chooseDark ? std::move(dark) : std::move(light);

    const std::size_t pixelCount = app.grayscale.size();
    if (selected.area < 8 || static_cast<std::size_t>(selected.area) >= pixelCount * 98 / 100) {
        updateStatus(app);
        return false;
    }

    const std::vector<Complex> boundary =
        traceBoundary(selected, app.analysisWidth, app.analysisHeight);
    app.contour = resampleClosedContour(boundary, app.analysisWidth, app.analysisHeight);
    if (app.contour.size() != kSampleCount) {
        app.contour.clear();
        updateStatus(app);
        return false;
    }

    app.terms = calculateFourierTerms(app.contour);
    findBestApproximation(app);
    updateStatus(app);
    InvalidateRect(app.window, nullptr, FALSE);
    return true;
}

bool loadPhoto(AppState& app, const std::wstring& path) {
    std::unique_ptr<Gdiplus::Bitmap> loaded(new Gdiplus::Bitmap(path.c_str(), FALSE));
    if (loaded->GetLastStatus() != Gdiplus::Ok ||
        loaded->GetWidth() == 0 || loaded->GetHeight() == 0) {
        return false;
    }

    app.image = loaded.release();
    app.imagePath = path;
    const int imageWidth = static_cast<int>(app.image->GetWidth());
    const int imageHeight = static_cast<int>(app.image->GetHeight());
    const double resize = std::min(1.0,
        static_cast<double>(kAnalysisSide) / std::max(imageWidth, imageHeight));
    app.analysisWidth = std::max(16, static_cast<int>(std::lround(imageWidth * resize)));
    app.analysisHeight = std::max(16, static_cast<int>(std::lround(imageHeight * resize)));

    app.grayscale.resize(static_cast<std::size_t>(app.analysisWidth) * app.analysisHeight);
    for (int y = 0; y < app.analysisHeight; ++y) {
        const UINT sourceY = std::min(app.image->GetHeight() - 1,
            static_cast<UINT>((static_cast<std::uint64_t>(y) * app.image->GetHeight()) /
                              app.analysisHeight));
        for (int x = 0; x < app.analysisWidth; ++x) {
            const UINT sourceX = std::min(app.image->GetWidth() - 1,
                static_cast<UINT>((static_cast<std::uint64_t>(x) * app.image->GetWidth()) /
                                  app.analysisWidth));
            Gdiplus::Color color;
            if (app.image->GetPixel(sourceX, sourceY, &color) != Gdiplus::Ok) {
                app.grayscale[y * app.analysisWidth + x] = 255;
                continue;
            }
            const int luminance =
                (299 * color.GetRed() + 587 * color.GetGreen() + 114 * color.GetBlue()) / 1000;
            app.grayscale[y * app.analysisWidth + x] = static_cast<unsigned char>(
                (luminance * color.GetAlpha() + 255 * (255 - color.GetAlpha())) / 255);
        }
    }

    app.threshold = calculateOtsuThreshold(app.grayscale);
    buildOutline(app, false);
    return true;
}

bool chooseAndLoadPhoto(AppState& app) {
    wchar_t path[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter =
        L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0"
        L"All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"Choose a photo to trace";
    if (!GetOpenFileNameW(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) {
            MessageBoxW(app.window, L"The file picker could not be opened.",
                L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> previous(app.image);
    app.image = nullptr;
    if (!loadPhoto(app, path)) {
        delete app.image;
        app.image = previous.release();
        MessageBoxW(app.window,
            L"The selected image could not be loaded. The previous image is unchanged.",
            L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        updateStatus(app);
        return false;
    }
    previous.reset();
    app.playing = true;
    SetWindowTextW(app.playButton, L"Pause");
    InvalidateRect(app.window, nullptr, FALSE);
    return true;
}

void layoutControls(AppState& app, int width) {
    MoveWindow(app.openButton, 12, 10, 112, 30, TRUE);
    MoveWindow(app.playButton, 132, 10, 82, 30, TRUE);
    MoveWindow(app.circleCountLabel, 222, 6, 195, 40, TRUE);
    MoveWindow(app.maximumLabel, 420, 14, 72, 22, TRUE);
    MoveWindow(app.maximumEdit, 494, 10, 64, 30, TRUE);
    MoveWindow(app.applyMaximumButton, 556, 10, 56, 30, TRUE);
    MoveWindow(app.statusLabel, 622, 12, std::max(120, width - 642), 24, TRUE);
}

void applyMaximumCircleCount(AppState& app) {
    BOOL translated = FALSE;
    const UINT requested = GetDlgItemInt(app.window, kMaximumEdit, &translated, FALSE);
    if (!translated || requested < 1 || requested > kMaximumCircles) {
        MessageBoxW(app.window, L"Enter a maximum circle count from 1 to 512.",
            L"Photo Circle Animator", MB_OK | MB_ICONWARNING);
        SetWindowTextW(app.maximumEdit, std::to_wstring(app.maximumCircles).c_str());
        SetFocus(app.maximumEdit);
        return;
    }

    app.maximumCircles = static_cast<int>(requested);
    const bool wasFinished = app.animationFinished;
    if (!app.terms.empty()) {
        app.circleCount = std::clamp(app.circleCount, 1, maximumCircleCount(app));
        app.similarity = measureSimilarity(app, app.circleCount);
        app.animationFinished = app.circleCount >= maximumCircleCount(app);
        if (wasFinished && !app.animationFinished) {
            app.playing = true;
        } else if (app.animationFinished) {
            app.playing = false;
        }
        SetWindowTextW(app.playButton, app.playing ? L"Pause" : L"Play");
    }
    app.phase = 0.0;
    app.trace.clear();
    app.completedTraces.clear();
    updateApproximationStats(app);
    updateStatus(app);
    InvalidateRect(app.window, nullptr, FALSE);
}

LRESULT CALLBACK maximumEditProcedure(HWND edit, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND parent = GetParent(edit);
        SendMessageW(parent, WM_COMMAND,
            MAKEWPARAM(kApplyMaximumButton, BN_CLICKED),
            reinterpret_cast<LPARAM>(edit));
        return 0;
    }

    const WNDPROC previousProcedure = g_app.maximumEditPreviousProcedure;
    if (message == WM_NCDESTROY && previousProcedure != nullptr) {
        SetWindowLongPtrW(edit, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(previousProcedure));
        g_app.maximumEditPreviousProcedure = nullptr;
    }
    return CallWindowProcW(previousProcedure, edit, message, wParam, lParam);
}

bool calculateImagePlacement(const AppState& app, const RECT& client, Gdiplus::RectF& destination,
    double& scale) {
    if (app.image == nullptr) {
        return false;
    }
    const float top = 52.0f;
    const float margin = 20.0f;
    const float areaWidth = std::max(1.0f, client.right - client.left - margin * 2.0f);
    const float areaHeight = std::max(1.0f, client.bottom - top - margin);
    const double imageWidth = app.image->GetWidth();
    const double imageHeight = app.image->GetHeight();
    const double fit = std::min(areaWidth / imageWidth, areaHeight / imageHeight);
    const float width = static_cast<float>(imageWidth * fit);
    const float height = static_cast<float>(imageHeight * fit);
    destination = Gdiplus::RectF(
        (client.right - width) / 2.0f,
        top + (areaHeight - height) / 2.0f,
        width,
        height);
    scale = std::min(width / app.analysisWidth, height / app.analysisHeight);
    return true;
}

Gdiplus::PointF mapPoint(
    const Complex& point,
    const Gdiplus::RectF& destination,
    double scale) {
    return Gdiplus::PointF(
        destination.X + destination.Width / 2.0f +
            static_cast<float>(point.real() * scale),
        destination.Y + destination.Height / 2.0f +
            static_cast<float>(point.imag() * scale));
}

void paintCanvas(AppState& app, HDC hdc) {
    RECT client{};
    GetClientRect(app.window, &client);
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.Clear(Gdiplus::Color(255, 0, 0, 0));

    if (app.image == nullptr) {
        Gdiplus::SolidBrush promptBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Font font(L"Segoe UI", 13.0f);
        graphics.DrawString(L"Open a photo to see its circle animation.",
            -1, &font, Gdiplus::PointF(24.0f, 76.0f), &promptBrush);
        return;
    }

    Gdiplus::RectF destination;
    double scale = 1.0;
    if (!calculateImagePlacement(app, client, destination, scale)) {
        return;
    }

    if (app.animationFinished) {
        graphics.DrawImage(app.image, destination, 0.0f, 0.0f,
            static_cast<float>(app.image->GetWidth()),
            static_cast<float>(app.image->GetHeight()),
            Gdiplus::UnitPixel);
        return;
    }

    if (app.terms.empty() || app.circleCount <= 0) {
        return;
    }

    Gdiplus::Pen circlePen(Gdiplus::Color(190, 255, 255, 255), 2.0f);
    Gdiplus::Pen armPen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
    Gdiplus::Pen tracePen(Gdiplus::Color(255, 255, 255, 255), 3.5f);
    Gdiplus::GraphicsPath circlePath;
    Gdiplus::GraphicsPath armPath;
    Complex endpoint{};
    const double angleScale = 2.0 * kPi * app.phase;
    for (int index = 0; index < app.circleCount; ++index) {
        const FourierTerm& term = app.terms[index];
        const Gdiplus::PointF center = mapPoint(endpoint, destination, scale);
        const double radius = std::max(1.5, std::abs(term.coefficient) * scale);
        if (radius < std::max(destination.Width, destination.Height)) {
            circlePath.AddEllipse(
                center.X - static_cast<float>(radius),
                center.Y - static_cast<float>(radius),
                static_cast<float>(2.0 * radius),
                static_cast<float>(2.0 * radius));
        }

        const double angle = angleScale * term.frequency;
        const Complex nextEndpoint =
            endpoint + term.coefficient * Complex(std::cos(angle), std::sin(angle));
        const Gdiplus::PointF nextCenter = mapPoint(nextEndpoint, destination, scale);
        armPath.AddLine(center, nextCenter);
        endpoint = nextEndpoint;
    }
    if (circlePath.GetPointCount() > 0) {
        graphics.DrawPath(&circlePen, &circlePath);
    }
    if (armPath.GetPointCount() > 0) {
        graphics.DrawPath(&armPen, &armPath);
    }

    for (const std::vector<Gdiplus::PointF>& completedTrace : app.completedTraces) {
        if (completedTrace.size() > 1) {
            graphics.DrawLines(&tracePen, completedTrace.data(),
                static_cast<INT>(completedTrace.size()));
        }
    }
    if (app.trace.size() > 1) {
        graphics.DrawLines(&tracePen, app.trace.data(), static_cast<INT>(app.trace.size()));
    }
    const Gdiplus::PointF endPoint = mapPoint(endpoint, destination, scale);
    Gdiplus::SolidBrush endpointBrush(Gdiplus::Color(255, 255, 255, 255));
    graphics.FillEllipse(&endpointBrush, endPoint.X - 4.0f, endPoint.Y - 4.0f, 8.0f, 8.0f);
}

double animationPhaseStep(const AppState& app) {
    const double multiplier = std::min(
        std::pow(kAnimationSpeedIncrease,
            std::min(app.completedLoops, kMaximumSpeedRampLoops)),
        kMaximumAnimationSpeedMultiplier);
    return kNormalPhaseStep * multiplier;
}

void advanceAnimation(AppState& app) {
    if (!app.playing || app.animationFinished || app.terms.empty() || app.circleCount <= 0) {
        return;
    }

    app.phase += animationPhaseStep(app);
    if (app.phase >= 1.0) {
        app.phase = 0.0;
        app.completedLoops = std::min(
            app.completedLoops + 1, kMaximumSpeedRampLoops);
        const int maximum = maximumCircleCount(app);
        const bool addingCircles = app.circleCount < maximum;
        if (addingCircles) {
            app.completedTraces.clear();
            app.trace.clear();
            app.circleCount = nextCircleCount(app.circleCount, maximum);
            app.similarity = measureSimilarity(app, app.circleCount);
        } else {
            if (app.trace.size() > 1) {
                app.trace.push_back(app.trace.front());
                app.completedTraces.push_back(std::move(app.trace));
                if (app.completedTraces.size() > kMaximumSavedTraces) {
                    app.completedTraces.erase(app.completedTraces.begin());
                }
            }
            app.trace.clear();
        }
        if (app.circleCount >= maximum) {
            app.animationFinished = true;
            app.playing = false;
            SetWindowTextW(app.playButton, L"Play");
        }
        app.targetReached = app.targetAvailable &&
            app.similarity >= kSimilarityTarget;
        updateStatus(app);
    }

    if (!app.animationFinished) {
        RECT client{};
        GetClientRect(app.window, &client);
        Gdiplus::RectF destination;
        double scale = 1.0;
        if (calculateImagePlacement(app, client, destination, scale)) {
            const Complex point = evaluateTerms(app.terms, app.circleCount, app.phase);
            app.trace.push_back(mapPoint(point, destination, scale));
        }
    }
    InvalidateRect(app.window, nullptr, FALSE);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_app.window = window;
        g_app.openButton = CreateWindowW(L"BUTTON", L"Open photo...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenButton)), nullptr, nullptr);
        g_app.playButton = CreateWindowW(L"BUTTON", L"Pause",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPlayButton)), nullptr, nullptr);
        g_app.circleCountLabel = CreateWindowW(L"STATIC", L"Circles: 0",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCircleCountLabel)), nullptr, nullptr);
        g_app.maximumLabel = CreateWindowW(L"STATIC", L"Max circles:",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaximumLabel)), nullptr, nullptr);
        g_app.maximumEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"512",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaximumEdit)), nullptr, nullptr);
        g_app.applyMaximumButton = CreateWindowW(L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyMaximumButton)),
            nullptr, nullptr);
        g_app.statusLabel = CreateWindowW(L"STATIC", L"Open a photo to begin.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)), nullptr, nullptr);
        if (g_app.openButton == nullptr || g_app.playButton == nullptr ||
            g_app.circleCountLabel == nullptr || g_app.maximumLabel == nullptr ||
            g_app.maximumEdit == nullptr || g_app.applyMaximumButton == nullptr ||
            g_app.statusLabel == nullptr) {
            return -1;
        }
        SendMessageW(g_app.maximumEdit, EM_SETLIMITTEXT, 3, 0);
        g_app.maximumEditPreviousProcedure = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_app.maximumEdit, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(maximumEditProcedure)));
        if (g_app.maximumEditPreviousProcedure == nullptr) {
            return -1;
        }
        updateCircleCountDisplay(g_app);
        HFONT largeFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (largeFont != nullptr) {
            SendMessageW(g_app.circleCountLabel, WM_SETFONT, reinterpret_cast<WPARAM>(largeFont), TRUE);
        }
        SetTimer(window, kAnimationTimer, 16, nullptr);
        RECT client{};
        GetClientRect(window, &client);
        layoutControls(g_app, client.right);
        return 0;
    }
    case WM_SIZE:
        layoutControls(g_app, LOWORD(lParam));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kOpenButton) {
            chooseAndLoadPhoto(g_app);
            return 0;
        }
        if (LOWORD(wParam) == kPlayButton) {
            if (g_app.animationFinished) {
                g_app.circleCount = kInitialCircleCount;
                g_app.completedLoops = 0;
                g_app.phase = 0.0;
                g_app.trace.clear();
                g_app.completedTraces.clear();
                g_app.animationFinished = false;
                g_app.similarity = measureSimilarity(g_app, g_app.circleCount);
                updateStatus(g_app);
                InvalidateRect(window, nullptr, FALSE);
            } else {
                g_app.playing = !g_app.playing;
            }
            SetWindowTextW(g_app.playButton, g_app.playing ? L"Pause" : L"Play");
            return 0;
        }
        if (LOWORD(wParam) == kApplyMaximumButton) {
            applyMaximumCircleCount(g_app);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kAnimationTimer) {
            advanceAnimation(g_app);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        HDC bufferDc = width > 0 && height > 0 ? CreateCompatibleDC(hdc) : nullptr;
        HBITMAP bufferBitmap = bufferDc != nullptr
            ? CreateCompatibleBitmap(hdc, width, height)
            : nullptr;
        HGDIOBJ previousBitmap = bufferBitmap != nullptr
            ? SelectObject(bufferDc, bufferBitmap)
            : nullptr;

        if (bufferDc != nullptr && bufferBitmap != nullptr &&
            previousBitmap != nullptr && previousBitmap != HGDI_ERROR) {
            paintCanvas(g_app, bufferDc);
            if (!BitBlt(hdc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY)) {
                paintCanvas(g_app, hdc);
            }
            SelectObject(bufferDc, previousBitmap);
        } else {
            paintCanvas(g_app, hdc);
        }

        if (bufferBitmap != nullptr) {
            DeleteObject(bufferBitmap);
        }
        if (bufferDc != nullptr) {
            DeleteDC(bufferDc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_blackBrush);
    case WM_DESTROY:
        KillTimer(window, kAnimationTimer);
        delete g_app.image;
        g_app.image = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+ could not be initialized.",
            L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    if (g_blackBrush == nullptr) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        MessageBoxW(nullptr, L"The application background could not be initialized.",
            L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = g_blackBrush;
    windowClass.lpszClassName = L"PhotoCircleAnimatorWindow";
    if (RegisterClassExW(&windowClass) == 0) {
        DeleteObject(g_blackBrush);
        g_blackBrush = nullptr;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        MessageBoxW(nullptr, L"The application window class could not be registered.",
            L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"Photo Circle Animator", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        UnregisterClassW(windowClass.lpszClassName, instance);
        DeleteObject(g_blackBrush);
        g_blackBrush = nullptr;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        MessageBoxW(nullptr, L"The application window could not be created.",
            L"Photo Circle Animator", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnregisterClassW(windowClass.lpszClassName, instance);
    DeleteObject(g_blackBrush);
    g_blackBrush = nullptr;
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(message.wParam);
}
