// linkcli — an interactive Ableton Link peer for bench work.
//
// The firmware is a Link *listener*: link_protocol.c parses the gossip timeline
// but never transmits, so a device alone on the network sees peers:0 and silently
// drops transport intents. This is the peer that makes it a session — the stand-in
// for Ableton on the bench.
//
// Unlike the SDK's linkhut example, this drives ableton::Link directly instead of
// through an audio engine, so there is no CoreAudio dependency and no click track.

#include <ableton/Link.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <termios.h>
#include <unistd.h>

namespace
{

constexpr auto kEsc = "\x1b[";
constexpr auto kReset = "\x1b[0m";
constexpr auto kBold = "\x1b[1m";
constexpr auto kDim = "\x1b[2m";
constexpr auto kRed = "\x1b[31m";
constexpr auto kGreen = "\x1b[32m";
constexpr auto kYellow = "\x1b[33m";
constexpr auto kCyan = "\x1b[36m";

constexpr double kMinTempo = 20.0;
constexpr double kMaxTempo = 999.0;

struct State
{
  std::atomic<bool> running{true};
  std::atomic<double> quantum{4.0};
  ableton::Link link;

  explicit State(const double tempo)
    : link(tempo)
  {
  }
};

// ---------------------------------------------------------------------------
// terminal
// ---------------------------------------------------------------------------

termios g_originalTermios;
bool g_termiosSaved = false;

void restoreTerminal()
{
  if (g_termiosSaved)
  {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_originalTermios);
    g_termiosSaved = false;
  }
  std::cout << "\x1b[?25h" << kReset << std::flush; // show cursor
}

void setupTerminal()
{
  if (tcgetattr(STDIN_FILENO, &g_originalTermios) == 0)
  {
    g_termiosSaved = true;
    termios raw = g_originalTermios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  }
  std::cout << "\x1b[?25l" << std::flush; // hide cursor
}

void onSignal(int)
{
  restoreTerminal();
  std::cout << std::endl;
  std::_Exit(0);
}

// ---------------------------------------------------------------------------
// session mutation — every write follows capture / mutate / commit
// ---------------------------------------------------------------------------

void setTempo(State& state, const double bpm)
{
  const auto clamped = std::min(kMaxTempo, std::max(kMinTempo, bpm));
  auto session = state.link.captureAppSessionState();
  session.setTempo(clamped, state.link.clock().micros());
  state.link.commitAppSessionState(session);
}

void nudgeTempo(State& state, const double delta)
{
  setTempo(state, state.link.captureAppSessionState().tempo() + delta);
}

void togglePlaying(State& state)
{
  auto session = state.link.captureAppSessionState();
  const auto now = state.link.clock().micros();
  session.setIsPlaying(!session.isPlaying(), now);
  state.link.commitAppSessionState(session);
}

// Land beat 0 on "now" — the bench operation you actually want when a device has
// drifted a bar away and you need a known downbeat to trigger the analyzer on.
void forceDownbeat(State& state)
{
  auto session = state.link.captureAppSessionState();
  const auto now = state.link.clock().micros();
  session.requestBeatAtTime(0.0, now, state.quantum.load());
  state.link.commitAppSessionState(session);
}

void nudgeQuantum(State& state, const double delta)
{
  const auto q = std::max(1.0, std::min(16.0, state.quantum.load() + delta));
  state.quantum.store(q);
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

std::string fixed(const double value, const int precision)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(precision) << value;
  return os.str();
}

std::string phaseMeter(const double phase, const double quantum)
{
  std::string meter;
  const auto slots = static_cast<int>(std::ceil(quantum));
  const auto current = static_cast<int>(phase);
  for (int i = 0; i < slots; ++i)
  {
    const bool isDownbeat = (i == 0);
    const bool isCurrent = (i == current);
    if (isCurrent)
    {
      meter += isDownbeat ? std::string(kCyan) + kBold : std::string(kGreen) + kBold;
      meter += "●";
      meter += kReset;
    }
    else
    {
      meter += std::string(kDim) + "○" + kReset;
    }
    if (i + 1 < slots)
    {
      meter += " ";
    }
  }
  return meter;
}

std::vector<std::string> renderFrame(State& state)
{
  const auto now = state.link.clock().micros();
  const auto session = state.link.captureAppSessionState();
  const auto quantum = state.quantum.load();
  const auto peers = state.link.numPeers();
  const auto beat = session.beatAtTime(now, quantum);
  const auto phase = session.phaseAtTime(now, quantum);

  const std::string enabled = state.link.isEnabled()
                                ? std::string(kGreen) + "on" + kReset
                                : std::string(kRed) + "OFF" + kReset;
  const std::string sss = state.link.isStartStopSyncEnabled()
                            ? std::string(kGreen) + "on" + kReset
                            : std::string(kDim) + "off" + kReset;
  const std::string transport = session.isPlaying()
                                  ? std::string(kGreen) + kBold + "▶ PLAYING" + kReset
                                  : std::string(kDim) + "■ stopped" + kReset;

  // peers:0 is the failure mode worth shouting about — a device alone on the wire
  // accepts /transport with a 200 and then drops the intent on the floor.
  const std::string peersCell =
    peers == 0 ? std::string(kRed) + kBold + "0  (no session!)" + kReset
               : std::string(kBold) + std::to_string(peers) + kReset;

  std::vector<std::string> lines;
  lines.emplace_back(std::string(kBold) + "  linkcli" + kReset + std::string(kDim)
                     + "  — Ableton Link peer" + kReset);
  lines.emplace_back("");
  lines.emplace_back("  peers    " + peersCell);
  lines.emplace_back("  tempo    " + std::string(kBold) + fixed(session.tempo(), 2) + kReset
                     + std::string(kDim) + " bpm" + kReset);
  lines.emplace_back("  quantum  " + fixed(quantum, 0));
  lines.emplace_back("  transport " + transport);
  lines.emplace_back("");
  lines.emplace_back("  beat     " + fixed(beat, 2) + std::string(kDim) + "   phase "
                     + kReset + fixed(phase, 2));
  lines.emplace_back("  " + phaseMeter(phase, quantum));
  lines.emplace_back("");
  lines.emplace_back(std::string(kDim)
                     + "  link " + kReset + enabled + std::string(kDim)
                     + "   start/stop sync " + kReset + sss);
  lines.emplace_back("");
  lines.emplace_back(std::string(kDim)
                     + "  ↑/↓ tempo ±1   ←/→ tempo ±0.1   q/Q quantum   space play/stop"
                     + kReset);
  lines.emplace_back(std::string(kDim)
                     + "  p force downbeat   s start/stop sync   a link on/off   x quit"
                     + kReset);
  return lines;
}

void draw(State& state, std::size_t& lastLineCount)
{
  const auto lines = renderFrame(state);
  std::ostringstream out;
  if (lastLineCount > 0)
  {
    out << kEsc << lastLineCount << "A"; // rewind to the top of the last frame
  }
  for (const auto& line : lines)
  {
    out << "\x1b[2K" << line << "\n"; // erase-line guards against short redraws
  }
  std::cout << out.str() << std::flush;
  lastLineCount = lines.size();
}

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------

void inputLoop(State& state)
{
  while (state.running)
  {
    char c = 0;
    const auto n = ::read(STDIN_FILENO, &c, 1);
    if (n == 0)
    {
      return; // stdin closed / not a tty — no more keys are coming, don't spin
    }
    if (n != 1)
    {
      continue;
    }

    if (c == '\x1b') // arrow keys arrive as ESC [ A..D
    {
      char seq[2] = {0, 0};
      if (::read(STDIN_FILENO, &seq[0], 1) != 1 || seq[0] != '[')
      {
        continue;
      }
      if (::read(STDIN_FILENO, &seq[1], 1) != 1)
      {
        continue;
      }
      switch (seq[1])
      {
      case 'A': nudgeTempo(state, 1.0); break;
      case 'B': nudgeTempo(state, -1.0); break;
      case 'C': nudgeTempo(state, 0.1); break;
      case 'D': nudgeTempo(state, -0.1); break;
      default: break;
      }
      continue;
    }

    switch (c)
    {
    case ' ': togglePlaying(state); break;
    case 'q': nudgeQuantum(state, 1.0); break;
    case 'Q': nudgeQuantum(state, -1.0); break;
    case 'p': forceDownbeat(state); break;
    case 's': state.link.enableStartStopSync(!state.link.isStartStopSyncEnabled()); break;
    case 'a': state.link.enable(!state.link.isEnabled()); break;
    case 'x':
    case 3: // ctrl-c
      state.running = false;
      return;
    default: break;
    }
  }
}

void printUsage()
{
  std::cout
    << "linkcli — interactive Ableton Link peer\n\n"
       "usage: linkcli [options]\n\n"
       "  --tempo <bpm>      initial tempo (default 120)\n"
       "  --quantum <beats>  bar length in beats (default 4)\n"
       "  --play             start the session playing\n"
       "  --no-sync          disable start/stop sync (on by default)\n"
       "  -h, --help         this message\n";
}

} // namespace

int main(int argc, char** argv)
{
  double tempo = 120.0;
  double quantum = 4.0;
  bool play = false;
  bool startStopSync = true;

  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    const bool hasNext = (i + 1 < argc);
    if ((arg == "--tempo") && hasNext)
    {
      tempo = std::atof(argv[++i]);
    }
    else if ((arg == "--quantum") && hasNext)
    {
      quantum = std::atof(argv[++i]);
    }
    else if (arg == "--play")
    {
      play = true;
    }
    else if (arg == "--no-sync")
    {
      startStopSync = false;
    }
    else if (arg == "-h" || arg == "--help")
    {
      printUsage();
      return 0;
    }
    else
    {
      std::cerr << "unknown argument: " << arg << "\n\n";
      printUsage();
      return 1;
    }
  }

  State state(std::min(kMaxTempo, std::max(kMinTempo, tempo)));
  state.quantum.store(std::max(1.0, std::min(16.0, quantum)));
  state.link.enable(true);
  state.link.enableStartStopSync(startStopSync);
  if (play)
  {
    togglePlaying(state);
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::atexit(restoreTerminal);
  setupTerminal();

  std::thread input(inputLoop, std::ref(state));

  std::size_t lastLineCount = 0;
  while (state.running)
  {
    draw(state, lastLineCount);
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }

  restoreTerminal();
  input.join();
  return 0;
}
