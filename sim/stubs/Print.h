#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

class Print
{
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t write(const char *str) { return str ? write((const uint8_t *)str, strlen(str)) : 0; }
  virtual size_t write(const uint8_t *buffer, size_t size)
  {
    size_t n = 0;
    while (size--)
      n += write(*buffer++);
    return n;
  }

  size_t print(const char *str) { return write(str); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int value) { return printFormatted("%d", value); }
  size_t print(unsigned int value) { return printFormatted("%u", value); }
  size_t print(long value) { return printFormatted("%ld", value); }
  size_t print(unsigned long value) { return printFormatted("%lu", value); }
  size_t print(double value, int digits = 2) { return printFormatted("%.*f", digits, value); }
  size_t print(bool value) { return print((int)value); }

  size_t println() { return write("\r\n"); }
  template <typename T> size_t println(T value) { return print(value) + println(); }

private:
  template <typename... Args> size_t printFormatted(const char *format, Args... args)
  {
    char buf[64];
    snprintf(buf, sizeof(buf), format, args...);
    return write(buf);
  }
};
