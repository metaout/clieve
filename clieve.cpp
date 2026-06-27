#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <random>
#include <vector>

class ConsoleModeScope {
  HANDLE _input;
  HANDLE _output;

  DWORD _inputMode;
  DWORD _outputMode;

public:
  ConsoleModeScope(HANDLE input, DWORD inputMode, HANDLE output, DWORD outputMode, int inherit) :
    _input(input), _output(output), _inputMode(0), _outputMode(0) {
    GetConsoleMode(_input, &_inputMode);
    GetConsoleMode(_output, &_outputMode);

    if (inherit & 0b10) {
      inputMode = _inputMode | inputMode;
    }

    if (inherit & 0b01) {
      outputMode = _outputMode | outputMode;
    }

    SetConsoleMode(_input, inputMode);
    SetConsoleMode(_output, outputMode);
  }

  ~ConsoleModeScope() {
    SetConsoleMode(_input, _inputMode);
    SetConsoleMode(_output, _outputMode);
  }
};

void write(HANDLE output, std::string_view text) {
  DWORD written = 0;
  WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

struct ScreenInfo {
  int cols;
  int rows;
  int left;
  int top;
};

class AlternateScreenScope {
  static constexpr std::string_view AlternateScreenBuffer = "\x1b[?1049h";
  static constexpr std::string_view HideCursor = "\x1b[?25l";
  static constexpr std::string_view ResetScrollRegion = "\x1b[r";
  static constexpr std::string_view ResetOriginMode = "\x1b[?6l";
  static constexpr std::string_view ClearScreen = "\x1b[2J";
  static constexpr std::string_view CursorHome = "\x1b[H";
  static constexpr std::string_view ResetStyle = "\x1b[0m";
  static constexpr std::string_view ShowCursor = "\x1b[?25h";
  static constexpr std::string_view LeaveAlternateScreenBuffer = "\x1b[?1049l";

  HANDLE _output;

public:
  AlternateScreenScope(HANDLE output) : _output(output) {
    write(_output, AlternateScreenBuffer);
    write(_output, HideCursor);
    write(_output, ResetScrollRegion);
    write(_output, ResetOriginMode);
    write(_output, ClearScreen);
    write(_output, CursorHome);
  }

  ~AlternateScreenScope() {
    write(_output, ResetStyle);
    write(_output, ShowCursor);
    write(_output, LeaveAlternateScreenBuffer);
  }

  ScreenInfo screenInfo() const {
    CONSOLE_SCREEN_BUFFER_INFO _info;
    GetConsoleScreenBufferInfo(_output, &_info);

    ScreenInfo info;
    info.cols = _info.srWindow.Right - _info.srWindow.Left + 1;
    info.rows = _info.srWindow.Bottom - _info.srWindow.Top + 1;
    info.left = _info.srWindow.Left;
    info.top = _info.srWindow.Top;

    return info;
  }
};

struct Color {
  int r;
  int g;
  int b;
};

struct Ripple {
  double x;
  double y;
  ULONGLONG birth;
  bool active;
};

class RippleState {
  static constexpr double duration = 2000.0;
  static constexpr size_t N = 12;

  std::array<Ripple, N> _ripples;
  size_t _index;

public:
  RippleState() : _ripples(), _index(0) {}

  void update(ScreenInfo& info, MOUSE_EVENT_RECORD mouse) {
    bool hasButton = mouse.dwButtonState != 0;
    bool hasClick = mouse.dwEventFlags == 0 && hasButton;

    if (hasClick) {
      int _x = mouse.dwMousePosition.X - info.left;
      int _y = mouse.dwMousePosition.Y - info.top;

      double x = std::clamp(_x, 0, info.cols - 1);
      double y = std::clamp(_y, 0, info.rows - 1);

      x = (x + 0.5) / static_cast<double>(info.cols);
      y = (y + 0.5) / static_cast<double>(info.rows);

      _ripples[_index].x = x;
      _ripples[_index].y = y;
      _ripples[_index].birth = GetTickCount64();
      _ripples[_index].active = true;

      _index = (_index + 1) % N;
    }
  }

  constexpr size_t size() const {
    return N;
  }

  Color operator()(size_t i) const {
    auto ripple = _ripples[i];

    if (!ripple.active) {
      return Color(0, 0, 255);
    }

    double age = static_cast<double>(GetTickCount64() - ripple.birth) / duration;

    int r = static_cast<int>(std::round(255.0 * std::clamp(ripple.x, 0.0, 1.0)));
    int g = static_cast<int>(std::round(255.0 * std::clamp(ripple.y, 0.0, 1.0)));
    int b = static_cast<int>(std::round(255.0 * std::clamp(age, 0.0, 1.0)));

    return Color(r, g, b);
  }
};

void stream(std::ostringstream& out, ScreenInfo screen) {
  static std::mt19937 rng(static_cast<unsigned int>(GetTickCount64()));
  static std::vector<ULONGLONG> starts;
  static std::vector<int> speeds;
  static std::vector<int> lengths;
  static std::vector<std::string> texts;
  static int cols = 0;
  static int rows = 0;

  if (cols != screen.cols || rows != screen.rows) {
    cols = screen.cols;
    rows = screen.rows;

    starts.resize(rows);
    speeds.resize(rows);
    lengths.resize(rows);
    texts.resize(rows);

    std::uniform_int_distribution<int> speedDist(1, 4);
    std::uniform_int_distribution<int> lengthDist(12, 32);
    std::uniform_int_distribution<int> delayDist(0, 2500);
    std::uniform_int_distribution<int> charDist(0, 61);

    static constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    ULONGLONG tick = GetTickCount64();

    for (int y = 0; y < rows; y++) {
      starts[y] = tick + static_cast<ULONGLONG>(delayDist(rng));
      speeds[y] = speedDist(rng);
      lengths[y] = lengthDist(rng);
      texts[y].clear();

      for (int i = 0; i < lengths[y]; i++) {
        texts[y].push_back(chars[charDist(rng)]);
      }
    }
  }

  ULONGLONG tick = GetTickCount64();

  for (int y = 2; y < screen.rows; y++) {
    out << "\x1b[" << y + 1 << ";1H";
    out << "\x1b[2K";

    if (tick < starts[y]) {
      continue;
    }

    int speed = speeds[y];
    int length = lengths[y];
    int head = static_cast<int>((tick - starts[y]) / 80 * speed) - length;

    if (head - length > screen.cols) {
      std::uniform_int_distribution<int> speedDist(1, 4);
      std::uniform_int_distribution<int> lengthDist(12, 32);
      std::uniform_int_distribution<int> delayDist(300, 2500);
      std::uniform_int_distribution<int> charDist(0, 61);

      static constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

      starts[y] = tick + static_cast<ULONGLONG>(delayDist(rng));
      speeds[y] = speedDist(rng);
      lengths[y] = lengthDist(rng);
      texts[y].clear();

      for (int i = 0; i < lengths[y]; i++) {
        texts[y].push_back(chars[charDist(rng)]);
      }

      continue;
    }

    for (int i = 0; i < length; i++) {
      int x = head - i;

      if (x < 0 || x >= screen.cols) {
        continue;
      }

      char c = texts[y][static_cast<size_t>(i)];

      out << "\x1b[" << y + 1 << ";" << x + 1 << "H";

      if (i == 0) {
        out << "\x1b[97m";
      } else if (i < 6) {
        out << "\x1b[37m";
      } else {
        out << "\x1b[90m";
      }

      out << c;
    }
  }

  out << "\x1b[0m";
}

void draw(HANDLE output, ScreenInfo screen, RippleState state) {
  int width = screen.cols / static_cast<int>(state.size());

  std::ostringstream out;
  out << "\x1b[" << 1 << ";1H";

  for (size_t i = 0; i < state.size(); i++) {
    auto color = state(i);
    out << "\x1b[48;2;" << color.r << ";" << color.g << ";" << color.b << "m";
    out << std::string(width, ' ');
  }

  out << "\x1b[0m";

  stream(out, screen);

  write(output, out.str());
}

int main() {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

  ConsoleModeScope cm(input, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT,
    output, ENABLE_VIRTUAL_TERMINAL_PROCESSING, 0b01);

  AlternateScreenScope altsc(output);
  auto info = altsc.screenInfo();

  // ---------------------------------- //
  RippleState state{};
  bool running = true;

  while (running) {
    DWORD wait = WaitForSingleObject(input, 16);

    if (wait == WAIT_OBJECT_0) {
      INPUT_RECORD records[64];
      DWORD read = 0;
      ReadConsoleInput(input, records, 64, &read);

      for (DWORD i = 0; i < read; i++) {
        auto record = records[i];

        if (record.EventType == KEY_EVENT) {
          auto key = record.Event.KeyEvent;
          if (key.bKeyDown && (key.wVirtualKeyCode == VK_ESCAPE || key.uChar.AsciiChar == 'q' || key.uChar.AsciiChar == 'Q')) {
            running = false;
          }
        }

        if (record.EventType == MOUSE_EVENT) {
          state.update(info, record.Event.MouseEvent);
        }

        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
          info = altsc.screenInfo();
        }
      }
    }

    draw(output, info, state);
  }
}