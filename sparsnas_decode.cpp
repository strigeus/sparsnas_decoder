#include <stdint.h>
#include <stdio.h>
#define _USE_MATH_DEFINES 1
#include <math.h>
#include <complex>
#include <string>
#include <time.h>


//////////////////////////////////////
#define PULSES_PER_KWH 1000

// These are the last 6 digits from the serial number of the sender.
// The serial number is located under the battery.
// The full serial number looks like "400 666 111"
#define SENSOR_ID 666111

// It seems like different RTL-SDR tune to slightly different frequencies
// Or I'm not really sure what's up, but the 0 and 1 frequencies differ
// between different RTL-SDR and/or sparsnäs. You can have a look at the 
// signal in a wave file editor and you can measure the wavelengths of the
// sine waves and put in appropriate values here.
//#define FREQUENCIES {12500.0,50000.0}
#define FREQUENCIES {67500.0,105000.0}
//#define FREQUENCIES {20000.0,60000.0}

//////////////////////////////////////

FILE *outfile;


// Implementation of Complex numbers, cause std::complex is stupid and doesn't inline properly.
template<typename T>
struct ComplexBase {
  T real, imag;

  static ComplexBase<T> Make(T a = 0.0f, T b = 0.0f) {
    ComplexBase<T> r = {a, b};
    return r;
  }
  friend ComplexBase<T> operator*(ComplexBase<T> a, T b) {
    ComplexBase<T> r = {a.real * b, a.imag * b};
    return r;
  }

  friend ComplexBase<T> operator*(ComplexBase<T> a, ComplexBase<T> b) {
    return (a *= b);
  }

  friend ComplexBase<T> operator/(ComplexBase<T> a, T b) {
    ComplexBase<T> r = {a.real / b, a.imag / b};
    return r;
  }
  friend ComplexBase<T> operator+(ComplexBase<T> a, ComplexBase<T> b) {
    ComplexBase<T> r = {a.real + b.real, a.imag + b.imag};
    return r;
  }
  friend ComplexBase<T> operator-(ComplexBase<T> a, ComplexBase<T> b) {
    ComplexBase<T> r = {a.real - b.real, a.imag - b.imag};
    return r;
  }
  ComplexBase<T> &operator*=(T b) {
    return real *= b, imag *= b, *this;
  }
  ComplexBase<T> &operator+=(T b) {
    return real += b, *this;
  }
  ComplexBase<T> &operator+=(ComplexBase<T> b) {
    return real += b.real, imag += b.imag, *this;
  }
  ComplexBase<T> &operator*=(ComplexBase<T> b) {
    T t = real * b.real - imag * b.imag;
    imag = real * b.imag + imag * b.real;
    real = t;
    return *this;
  }

  T Abs() const {
    return hypotf(real, imag);
  }
};

typedef ComplexBase<float> Complex;
typedef ComplexBase<double> ComplexDouble;

uint16_t crc16(const uint8_t *data, size_t n) {
  uint16_t crcReg = 0xffff;
  size_t i, j;
  for (j = 0; j < n; j++) {
    uint8_t crcData = data[j];
    for (i = 0; i < 8; i++) {
      if (((crcReg & 0x8000) >> 8) ^ (crcData & 0x80))
        crcReg = (crcReg << 1) ^ 0x8005;
      else
        crcReg = (crcReg << 1);
      crcData <<= 1;
    }
  }
  return crcReg;
}


class SignalDetector {

public:
  SignalDetector() {
    shift_ = 0;
    found_sync_ = 0;
  }

  void add(bool v) {
    shift_ = shift_ * 2 + v;
    switch (found_sync_) {
    case 0:
      if ((shift_ & 0xFF) == 0xAA)
        found_sync_ = 1;
      break;
    case 1:
      if (shift_ == 0xAAAAD201)
        found_sync_ = 2;
      break;
    default:
      if (bits_ < 256) {
        data_[bits_ >> 3] = data_[bits_ >> 3] * 2 + v;
        bits_++;
      }
      break;
    }
  }

  bool has_some_sync() {
    return found_sync_ != 0;
  }

  void add_fail(float freq) {
    char mesg[1024];

    shift_ = 0;
    found_sync_ = 0;
    if (bits_ >= 160) {
      uint16_t crc = crc16(data_, 18);
      uint16_t packet_crc = data_[18] << 8 | data_[19];
      char *m = mesg;

      time_t mytime;
      mytime = time(NULL);
      struct tm * timeinfo;
      timeinfo = localtime(&mytime);

      m += sprintf(m, "[%.4d-%.2d-%.2d %.2d:%.2d:%.2d] ", timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday, 
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec);

      uint8_t dec[32];

      uint8_t enc_key[5];
      const uint32_t sensor_id_sub = SENSOR_ID - 0x5D38E8CB;

      enc_key[0] = (uint8_t)(sensor_id_sub >> 24);
      enc_key[1] = (uint8_t)(sensor_id_sub);
      enc_key[2] = (uint8_t)(sensor_id_sub >> 8);
      enc_key[3] = 0x47;
      enc_key[4] = (uint8_t)(sensor_id_sub >> 16);

      for(size_t i = 0; i < 13; i++)
        dec[5 + i] = data_[5 + i] ^ enc_key[i % 5];

      uint32_t rcv_sensor_id = dec[5] << 24 | dec[6] << 16 | dec[7] << 8 | dec[8];

      if (data_[0] != 0x11 || data_[1] != (SENSOR_ID & 0xFF) || data_[3] != 0x07 || data_[4] != 0x0E || rcv_sensor_id != SENSOR_ID) {
        m += sprintf(m, "Bad: ");
        for (int i = 0; i < 18; i++)
          m += sprintf(m, "%.2X ", data_[i]);
      } else {
        int seq = (dec[9] << 8 | dec[10]);
        int effect = (dec[11] << 8 | dec[12]);
        int pulse = (dec[13] << 24 | dec[14] << 16 | dec[15] << 8 | dec[16]);
        int battery = dec[17];
        float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);
        m += sprintf(m, "%5d: %7.1f W. %d.%.3d kWh. Batt %d%%. FreqErr: %.2f", seq, watt, pulse/1000, pulse%1000, battery, freq);
      }

      m += sprintf(m, (crc == packet_crc) ? "\n" : "CRC ERR\n");

      fprintf(stderr, "%s", mesg);
      if (outfile) {
        fprintf(outfile, "%s", mesg);
        fflush(outfile);
      }
    }
    bits_ = 0;
  }
  uint32_t shift_;
  uint8_t found_sync_;

  uint8_t data_[32];
  uint32_t bits_;
};


SignalDetector sd;

int main(int argc, char **argv)
{
  FILE *f = stdin;
  if (argc >= 2) {
    f = fopen(argv[1], "rb");
    if (!f) {
      fprintf(stderr, "Failed load!\n");
      return 1;
    }
//    for loading wav files
//    fseek(f, 44, SEEK_SET);
  }
  outfile = fopen("sparsnas.log", "a");

  FILE *logfile = NULL;// fopen("logfile.pcm", "wb");

  uint8_t buf[16384];
  Complex hist1[27] = { 0 };
  Complex hist2[27] = { 0 };
  Complex sum1 = {0, 0};
  Complex sum2 = {0, 0};

  float frequencies[] = FREQUENCIES;
  int hi = 0;

  float F1 = frequencies[0];
  float F2 = frequencies[1];
  float S = 1024000.0;
  int j = 0;

  bool last_signal = false;
  int last_sigtime = 0;
  
  const float PERFECT_PULSE_LEN = 26.6666666f * S / 1024000.0;
  const int MIN_PULSE_LEN = 12 * S / 1024000.0;
  const int MAX_PULSE_LEN = 42 * S / 1024000.0;
  float avg_err = 0;


  Complex c1 = {1, 0};
  Complex c2 = {1, 0};

  float f1 = 2 * M_PI * F1 / S;
  Complex rot1 = Complex::Make(cosf(f1), sinf(f1));
  float f2 = 2 * M_PI * F2 / S;
  Complex rot2 = Complex::Make(cosf(f2), sinf(f2));

  for (;;) {
    int elems = fread(buf, 2, 8192, f);
    if (elems <= 0)
      break;

    if (j - last_sigtime > (int)(200 * PERFECT_PULSE_LEN)) {
      // inject some trailing bits
      for(int i = 0;  i < 100; i++)
        sd.add(last_signal);
      sd.add_fail(avg_err);
      avg_err = 0;
    }

    for (int ei = 0; ei < elems; ei++, j++) {
      Complex v = {(float)(buf[ei * 2 + 0] - 128), (float)(buf[ei * 2 + 1] - 128)};

      Complex v1 = v * c1;
      Complex v2 = v * c2;

      sum1 += v1 - hist1[hi];
      hist1[hi] = v1;
      sum2 += v2 - hist2[hi];
      hist2[hi] = v2;

      c1 *= rot1;
      c2 *= rot2;

      if (++hi == 27)
        hi = 0;

      bool signal = sum1.real * sum1.real + sum1.imag * sum1.imag >
                    sum2.real * sum2.real + sum2.imag * sum2.imag;

      if (logfile) {short x = signal ? 10000 : -10000; fwrite(&x, 2, 1, logfile); }

      if (signal != last_signal) {
        int pulse_len = (unsigned)j - last_sigtime;
        
        if (pulse_len >= MIN_PULSE_LEN && (sd.has_some_sync() || pulse_len < MAX_PULSE_LEN)) {
          if (signal)
            avg_err = -avg_err;
          int syms2 = int((pulse_len - avg_err) * (1.0f / PERFECT_PULSE_LEN) + 0.5f);
          if (syms2 < 1) syms2 = 1;
          avg_err += (pulse_len - syms2 * PERFECT_PULSE_LEN - avg_err) * 0.1f;
          if (signal)
            avg_err = -avg_err;
          for (int i = 0; i < syms2; i++)
            sd.add(last_signal);
        } else {
          sd.add_fail(avg_err);
          avg_err = 0;
        }
        last_signal = signal;
        last_sigtime = j;
      }
    }

    c1 *= 1.0f / c1.Abs();
    c2 *= 1.0f / c2.Abs();
  }

  sd.add_fail(avg_err);

  return 0;
}



