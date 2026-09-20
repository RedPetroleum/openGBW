// Display simulator: runs the display, menu and game code of the firmware on the computer and saves
// the display content as PNG. Input comes from a script (knob, button, scale), see README.md in this folder.

#include <cctype>
#include <cerrno>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include "config.hpp"
#include "display.hpp"
#include "rotary.hpp"
#include "scale.hpp"

// ---------------------------------------------------------------------------------------------------------------
// Arduino and hardware stubs

static unsigned long simTime = 0;
static bool verbose = false;

unsigned long millis() { return simTime; }
unsigned long micros() { return simTime * 1000; }
void delay(unsigned long ms) { simTime += ms; }

long random(long max) { return max <= 0 ? 0 : rand() % max; }
long random(long min, long max) { return min >= max ? min : min + rand() % (max - min); }
void randomSeed(unsigned long seed) { srand(seed); }
long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

HardwareSerial Serial;

size_t HardwareSerial::write(uint8_t c)
{
  if (verbose)
    fputc(c, stderr);
  return 1;
}

size_t HardwareSerial::printf(const char *format, ...)
{
  if (!verbose)
    return 0;
  va_list args;
  va_start(args, format);
  int n = vfprintf(stderr, format, args);
  va_end(args);
  return n;
}

// The display is not connected, U8g2 only fills its buffer
extern "C" uint8_t u8x8_gpio_and_delay_arduino(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
extern "C" uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
void u8x8_SetPin_HW_I2C(u8x8_t *u8x8, uint8_t reset, uint8_t clock, uint8_t data)
{
  u8x8_SetPin(u8x8, U8X8_PIN_RESET, reset);
  u8x8_SetPin(u8x8, U8X8_PIN_I2C_CLOCK, clock);
  u8x8_SetPin(u8x8, U8X8_PIN_I2C_DATA, data);
}

// Globals of main.cpp
Preferences preferences;
HX711 loadcell;
SimpleKalmanFilter kalmanFilter(0.02, 0.02, 0.01);
SimpleKalmanFilter kalmanV01(FILTER_V01_KALMAN_ERROR, FILTER_V01_KALMAN_ERROR, FILTER_V01_KALMAN_NOISE);
TaskHandle_t ScaleTask = nullptr;
TaskHandle_t ScaleStatusTask = nullptr;
volatile bool displayLock = false;

void addGrindRecord(uint32_t shot, float duration, float grinderDelay, float flow, float target,
                    float actual); // scale.cpp

// ---------------------------------------------------------------------------------------------------------------
// PNG output

static void pngChunk(FILE *file, const char *type, const std::vector<uint8_t> &data)
{
  uint8_t header[8] = {(uint8_t)(data.size() >> 24), (uint8_t)(data.size() >> 16), (uint8_t)(data.size() >> 8), (uint8_t)data.size()};
  memcpy(header + 4, type, 4);
  fwrite(header, 1, 8, file);
  fwrite(data.data(), 1, data.size(), file);
  uLong crc = crc32(0, header + 4, 4);
  crc = crc32(crc, data.data(), data.size());
  uint8_t crcBytes[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
  fwrite(crcBytes, 1, 4, file);
}

// Saves the display buffer as grayscale PNG, every display pixel becomes a block of scale x scale pixels
static bool saveScreenshot(const std::string &path, int scale)
{
  u8g2_t *u8g2 = screen.getU8g2();
  int width = u8g2_GetDisplayWidth(u8g2);
  int height = u8g2_GetDisplayHeight(u8g2);
  uint8_t *buffer = u8g2_GetBufferPtr(u8g2);
  uint8_t tileWidth = u8g2_GetBufferTileWidth(u8g2);

  int outWidth = width * scale;
  int outHeight = height * scale;
  std::vector<uint8_t> raw;
  raw.reserve((outWidth + 1) * outHeight);
  for (int y = 0; y < outHeight; y++)
  {
    raw.push_back(0); // filter type
    for (int x = 0; x < outWidth; x++)
      raw.push_back(u8x8_capture_get_pixel_1(x / scale, y / scale, buffer, tileWidth) ? 255 : 0);
  }
  uLongf compressedSize = compressBound(raw.size());
  std::vector<uint8_t> compressed(compressedSize);
  compress2(compressed.data(), &compressedSize, raw.data(), raw.size(), 9);
  compressed.resize(compressedSize);

  FILE *file = fopen(path.c_str(), "wb");
  if (!file)
    return false;
  static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  fwrite(signature, 1, 8, file);
  std::vector<uint8_t> ihdr = {
      (uint8_t)(outWidth >> 24), (uint8_t)(outWidth >> 16), (uint8_t)(outWidth >> 8), (uint8_t)outWidth,
      (uint8_t)(outHeight >> 24), (uint8_t)(outHeight >> 16), (uint8_t)(outHeight >> 8), (uint8_t)outHeight,
      8, 0, 0, 0, 0}; // 8 bit grayscale
  pngChunk(file, "IHDR", ihdr);
  pngChunk(file, "IDAT", compressed);
  pngChunk(file, "IEND", {});
  return fclose(file) == 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Simulation

#define SIM_FRAME_MS 30         // time of one display update outside of games (games pace themselves)
#define SIM_SCALE_READING_MS 100 // interval of new scale readings
#define SIM_CLICK_MS 60         // how long a click holds the button down
#define SIM_CLICK_PAUSE_MS 600  // pause after a click outside of games, rapid clicks would toggle the debug mode
#define SIM_BOOT_MS 3100        // a little longer than BOOT_READY_MS of display.cpp, the initializing screen

static std::string outputDir = "sim/screenshots";
static int pixelScale = 4;
static unsigned long lastReadingAt = 0;
static double readingNoise = 0; // g, the scatter "noise" adds to every raw reading

// A reading of the load cell: the weight of the script, with the scatter of a real scale on it where
// the script asked for one. Only the raw readings carry it - the simulator runs no filter, so the
// weight the screens show stays the clean one the script set
static double reading()
{
  if (readingNoise <= 0)
    return scaleWeight;
  // Box-Muller, two uniform numbers into one normally distributed one
  double first = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
  double second = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
  return scaleWeight + readingNoise * sqrt(-2 * log(first)) * cos(2 * M_PI * second);
}

// One pass of the display task, with the knob polled and the scale read like the firmware tasks do
static void step()
{
  unsigned long before = simTime;
  rotary_loop();
  refreshDisplay();
  if (simTime == before)
    simTime += SIM_FRAME_MS;
  if (simTime - lastReadingAt >= SIM_SCALE_READING_MS)
  {
    double raw = reading();
    weightData.push(scaleWeight);
    rawData.push(raw); // no filter in the simulator, the raw buffer only differs by the added noise
    recordSwitchOffReading(raw); // like the sampler of the firmware does with every reading
    noiseSample(raw);
    scaleLastUpdatedAt = simTime;
    lastReadingAt = simTime;
  }
}

static void run(unsigned long ms)
{
  unsigned long until = simTime + ms;
  while (simTime < until)
    step();
}

static void setup()
{
  srand(1); // same game every run
  simTime = 1000;
  setupDisplay();
  setupScale();
  scaleReady = true;
  scaleLastUpdatedAt = simTime;
  lastActivityAt = simTime;
  weightData.push(scaleWeight);
  rawData.push(scaleWeight); // no filter in the simulator, both buffers get the same value
  lastReadingAt = simTime;
  step(); // replaces the welcome message with the main screen
}

static bool fail(int line, const std::string &message)
{
  fprintf(stderr, "sim: line %d: %s\n", line, message.c_str());
  return false;
}

static bool parseNumber(const std::string &text, double &value)
{
  char *end;
  value = strtod(text.c_str(), &end);
  return !text.empty() && *end == 0;
}

static bool setVariable(const std::string &name, const std::string &text, int line)
{
  static std::string failReason; // keeps the text alive for grindFailReason
  if (name == "grindFailReason")
  {
    failReason = text;
    grindFailReason = failReason.c_str();
    return true;
  }
  double value;
  if (!parseNumber(text, value))
    return fail(line, "not a number: " + text);
  if (name == "scaleWeight")
    scaleWeight = shownWeight = value; // the filter does not run here, the display reads shownWeight
  else if (name == "setWeight")
    setWeight = value;
  else if (name == "delayEnd")
    delayEnd = value;
  else if (name == "grindFlow")
    grindFlow = value;
  else if (name == "cupWeightEmpty")
    cupWeightEmpty = value;
  else if (name == "setCupWeight")
    setCupWeight = value;
  else if (name == "setCupWeight2")
    setCupWeight2 = value;
  else if (name == "scaleFactor")
    scaleFactor = value;
  else if (name == "noiseSigma")
    noiseSigma = value;
  else if (name == "shotCount")
    shotCount = value;
  else if (name == "sleepTime")
    sleepTime = value;
  else if (name == "scaleReady")
    scaleReady = value != 0;
  else if (name == "debugMode")
    debugMode = value != 0, menuItemsCount = debugMode ? 13 : 12;
  else if (name == "scaleMode")
    scaleMode = value != 0;
  else if (name == "grindMode")
    grindMode = value != 0;
  else if (name == "grindScreenStyle")
    grindScreenStyle = value;
  else if (name == "bootScreenStyle")
    bootScreenStyle = value;
  else if (name == "scaleStatus")
    scaleStatus = value;
  else if (name == "currentSetting")
    currentSetting = value;
  else if (name == "currentMenuItem")
    currentMenuItem = value;
  else if (name == "delayUsed")
    delayUsed = value;
  else if (name == "delayMeasured")
    delayMeasured = value;
  else if (name == "confirmedDose")
    confirmedDose = value;
  else if (name == "flowAtSwitchOff")
    flowAtSwitchOff = value;
  else if (name == "doseAtSwitchOff")
    doseAtSwitchOff = value;
  else if (name == "verifiedAgo") // seconds since the readings count towards the confirmed dose
    doseVerifiedFrom = simTime - (unsigned long)(value * 1000);
  else if (name == "grindTime") // seconds since grinding started (and finished grinds took)
  {
    startedGrindingAt = simTime - (unsigned long)(value * 1000);
    finishedGrindingAt = simTime;
  }
  else
    return fail(line, "unknown variable: " + name);
  return true;
}

static bool execute(const std::vector<std::string> &words, int line)
{
  const std::string &command = words[0];
  double value = 0;
  auto numberArgument = [&](size_t index) {
    return words.size() > index && parseNumber(words[index], value);
  };

  if (command == "wait")
  {
    if (!numberArgument(1))
      return fail(line, "usage: wait <ms>");
    run(value);
  }
  else if (command == "turn")
  {
    if (!numberArgument(1))
      return fail(line, "usage: turn <detents>");
    // One detent per display update, like turning the knob by hand
    int steps = value;
    for (int i = 0; i < abs(steps); i++)
    {
      rotaryEncoder.position += steps > 0 ? 1 : -1;
      step();
    }
  }
  else if (command == "click")
  {
    bool wasInGame = scaleStatus == STATUS_GAME;
    rotaryEncoder.buttonDown = true;
    run(SIM_CLICK_MS);
    rotaryEncoder.buttonDown = false;
    rotaryEncoder.clicked = true;
    step();
    if (!wasInGame && scaleStatus != STATUS_GAME)
      run(SIM_CLICK_PAUSE_MS);
  }
  else if (command == "hold")
  {
    if (!numberArgument(1))
      return fail(line, "usage: hold <ms>");
    rotaryEncoder.buttonDown = true;
    run(value);
    rotaryEncoder.buttonDown = false;
    rotaryEncoder.clicked = true; // the encoder library reports a long press as click on the release too
    step();
  }
  else if (command == "press")
  {
    rotaryEncoder.buttonDown = true;
  }
  else if (command == "release")
  {
    rotaryEncoder.buttonDown = false;
    rotaryEncoder.clicked = true;
  }
  else if (command == "weight")
  {
    if (!numberArgument(1))
      return fail(line, "usage: weight <grams>");
    scaleWeight = shownWeight = value;
  }
  else if (command == "noise")
  {
    // Scatter added to every raw reading from here on, what a real load cell does, 0 turns it off
    if (!numberArgument(1))
      return fail(line, "usage: noise <sigma in grams>");
    readingNoise = value;
  }
  else if (command == "set")
  {
    if (words.size() < 3)
      return fail(line, "usage: set <variable> <value>");
    std::string text = words[2];
    for (size_t i = 3; i < words.size(); i++)
      text += " " + words[i];
    return setVariable(words[1], text, line);
  }
  else if (command == "grind")
  {
    // grind <shot> <seconds> <delay> <flow> <target> <actual>: adds an entry to the Weight History
    double shot, duration, grinderDelay, flow, target, actual;
    if (words.size() != 7 || !parseNumber(words[1], shot) || !parseNumber(words[2], duration) ||
        !parseNumber(words[3], grinderDelay) || !parseNumber(words[4], flow) ||
        !parseNumber(words[5], target) || !parseNumber(words[6], actual))
      return fail(line, "usage: grind <shot> <seconds> <delay> <flow> <target> <actual>");
    addGrindRecord(shot, duration, grinderDelay, flow, target, actual);
  }
  else if (command == "boot")
  {
    // boot <ms>: shows the boot screen for this long, the scale has not reported a reading yet
    if (!numberArgument(1))
      return fail(line, "usage: boot <ms>");
    unsigned long until = simTime + (unsigned long)value;
    while (simTime < until)
    {
      scaleLastUpdatedAt = 0;
      step();
    }
    scaleLastUpdatedAt = simTime;
  }
  else if (command == "draw")
  {
    refreshDisplay(); // one more display update without time passing
  }
  else if (command == "shot")
  {
    if (words.size() != 2)
      return fail(line, "usage: shot <name>");
    std::string path = outputDir + "/" + words[1] + ".png";
    if (!saveScreenshot(path, pixelScale))
      return fail(line, "cannot write " + path);
    printf("%s\n", path.c_str());
  }
  else
    return fail(line, "unknown command: " + command);
  return true;
}

struct ScriptLine
{
  int number;
  std::vector<std::string> words;
};

// A script is a list of commands, separated by new lines or ";". "#" starts a comment.
// "[name]" starts a section: every section runs from a freshly started firmware.
static void parseScript(const std::string &text, std::vector<std::vector<ScriptLine>> &sections)
{
  int number = 1;
  size_t pos = 0;
  while (pos <= text.size())
  {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos)
      end = text.size();
    std::string lineText = text.substr(pos, end - pos);
    size_t comment = lineText.find('#');
    if (comment != std::string::npos)
      lineText.resize(comment);

    size_t start = lineText.find_first_not_of(" \t\r");
    if (start != std::string::npos && lineText[start] == '[')
    {
      sections.emplace_back();
    }
    else
    {
      size_t commandStart = 0;
      while (commandStart <= lineText.size())
      {
        size_t commandEnd = lineText.find(';', commandStart);
        if (commandEnd == std::string::npos)
          commandEnd = lineText.size();
        std::vector<std::string> words;
        std::string word;
        for (size_t i = commandStart; i <= commandEnd; i++)
        {
          if (i == commandEnd || isspace((unsigned char)lineText[i]))
          {
            if (!word.empty())
              words.push_back(word);
            word.clear();
          }
          else
            word += lineText[i];
        }
        if (!words.empty())
        {
          if (sections.empty())
            sections.emplace_back();
          sections.back().push_back({number, words});
        }
        commandStart = commandEnd + 1;
      }
    }
    pos = end + 1;
    number++;
  }
}

static bool runSection(const std::vector<ScriptLine> &lines)
{
  setup();
  // The initializing screen stays up for BOOT_READY_MS whatever the scale reports, so a section that
  // does not ask for it with "boot" waits it out first - otherwise every shot would show the cup
  bool showsBoot = false;
  for (const ScriptLine &line : lines)
    showsBoot = showsBoot || (!line.words.empty() && line.words[0] == "boot");
  if (!showsBoot)
    run(SIM_BOOT_MS);
  for (const ScriptLine &line : lines)
  {
    if (!execute(line.words, line.number))
      return false;
  }
  return true;
}

static void usage()
{
  fprintf(stderr,
          "usage: opengbw-sim [-o dir] [-s scale] [-v] [-f script]... [command]...\n"
          "  -o dir     folder for the screenshots (default sim/screenshots)\n"
          "  -s scale   size of one display pixel in the PNG (default 4)\n"
          "  -v         print the serial output of the firmware\n"
          "  -f script  run the commands of a script file\n"
          "commands (separate with \";\"): wait <ms>, turn <detents>, click, hold <ms>, press, release, weight <grams>,\n"
          "  noise <sigma>,\n"
          "  set <variable> <value>, grind <shot> <seconds> <delay> <flow> <target> <actual>, draw, shot <name>\n");
}

int main(int argc, char **argv)
{
  std::string script;
  int opt;
  while ((opt = getopt(argc, argv, "o:s:vf:h")) != -1)
  {
    switch (opt)
    {
    case 'o':
      outputDir = optarg;
      break;
    case 's':
      pixelScale = max(1, atoi(optarg));
      break;
    case 'v':
      verbose = true;
      break;
    case 'f':
    {
      FILE *file = fopen(optarg, "r");
      if (!file)
      {
        fprintf(stderr, "sim: cannot open %s\n", optarg);
        return 1;
      }
      char chunk[4096];
      size_t n;
      while ((n = fread(chunk, 1, sizeof(chunk), file)) > 0)
        script.append(chunk, n);
      fclose(file);
      script += "\n";
      break;
    }
    default:
      usage();
      return opt == 'h' ? 0 : 1;
    }
  }
  for (int i = optind; i < argc; i++)
  {
    std::string command = argv[i];
    std::replace(command.begin(), command.end(), ';', '\n');
    script += command + "\n";
  }
  if (script.find_first_not_of(" \t\r\n") == std::string::npos)
  {
    usage();
    return 1;
  }
  if (mkdir(outputDir.c_str(), 0755) != 0 && errno != EEXIST)
  {
    fprintf(stderr, "sim: cannot create %s\n", outputDir.c_str());
    return 1;
  }

  std::vector<std::vector<ScriptLine>> sections;
  parseScript(script, sections);

  // Every section runs in its own process, so it starts with the initial state of all firmware globals
  bool ok = true;
  for (const auto &section : sections)
  {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0)
    {
      bool sectionOk = runSection(section);
      fflush(stdout);
      _exit(sectionOk ? 0 : 1);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
      ok = false;
  }
  return ok ? 0 : 1;
}
