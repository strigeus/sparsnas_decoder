#include <stdint.h>
#include <stdio.h>
#define _USE_MATH_DEFINES 1
#include <math.h>
#include <complex>
#include <string>
#include <time.h>


//////////////////////////////////////
#define PULSES_PER_KWH 1000

// The packet is encrypted using the SENSOR_ID using a weak XOR
// encryption so you can guess it through a known plaintext attack.
#define SENSOR_ID 0xA29FF

//////////////////////////////////////

FILE *outfile;

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
    found_sync_ = false;
  }

  void add(bool v) {
    shift_ = shift_ * 2 + v;
    if ((shift_) == 0xAAAAD201 && !found_sync_)
      found_sync_ = true;
    else if (found_sync_ && bits_ < 256) {
      data_[bits_ >> 3] = data_[bits_ >> 3] * 2 + v;
      bits_++;
    }
  }

  void add_fail() {
    char mesg[1024];

    shift_ = 0;
    found_sync_ = false;
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

      if (data_[0] != 0x11 || data_[1] != 0xFF || data_[3] != 0x07 || data_[4] != 0x0E || rcv_sensor_id != SENSOR_ID) {
        m += sprintf(m, "Bad: ");
        for (int i = 0; i < 18; i++)
          m += sprintf(m, "%.2X ", data_[i]);
      } else {
        int seq = (dec[9] << 8 | dec[10]);
        int effect = (dec[11] << 8 | dec[12]);
        int pulse = (dec[13] << 24 | dec[14] << 16 | dec[15] << 8 | dec[16]);
        int battery = dec[17];
        float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);
        m += sprintf(m, "%5d: %7.1f W. %d.%.3d kWh. Batt %d%%.", seq, watt, pulse/1000, pulse%1000, battery);
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
  bool found_sync_;

  uint8_t data_[32];
  uint32_t bits_;
};


SignalDetector sd;

int main(int argc, char **argv)
{
  FILE *f = stdin;
  if (argc >= 2) {
    f = fopen(argv[2], "rb");
    if (!f) {
      fprintf(stderr, "Failed load!\n");
      return 1;
    }
  }
  outfile = fopen("sparsnas.log", "a");

  uint8_t buf[16384];
  std::complex<float> hist1[27] = { 0 };
  std::complex<float> hist2[27] = { 0 };
  std::complex<float> sum1(0, 0);
  std::complex<float> sum2(0, 0);
  int hi = 0;

  float F1 = 67000.0;
  float F2 = 105000.0;
  float S = 1024000.0;
  int j = 0;

  bool last_signal = false;
  int last_sigtime = 0;
  float avg = 26.6666666f;

  std::complex<float> c1(1, 0);
  std::complex<float> c2(1, 0);

  std::complex<float> rot1 = std::exp(std::complex<float>(0, 2 * M_PI * F1 / S));
  std::complex<float> rot2 = std::exp(std::complex<float>(0, 2 * M_PI * F2 / S));

  for (;;) {
    int elems = fread(buf, 2, 8192, f);
    if (elems <= 0)
      break;

    for (int ei = 0; ei < elems; ei++, j++) {
      std::complex<float> v(buf[ei * 2 + 0] - 128, buf[ei * 2 + 1] - 128);

      std::complex<float> v1 = v * c1;
      std::complex<float> v2 = v * c2;


      sum1 += v1 - hist1[hi];
      hist1[hi] = v1;
      sum2 += v2 - hist2[hi];
      hist2[hi] = v2;

      c1 *= rot1;
      c2 *= rot2;

      if (++hi == 27)
        hi = 0;

      bool signal = sum1.real() * sum1.real() + sum1.imag() * sum1.imag() >
                    sum2.real() * sum2.real() + sum2.imag() * sum2.imag();

      if (signal != last_signal) {
        int pulse_len = (unsigned)j - last_sigtime;
        if (pulse_len >= 20) {
          int syms = int(pulse_len / avg + 0.5f);
          avg = avg* 0.95f + (pulse_len / syms) * 0.05f;
          for (int i = 0; i < syms; i++)
            sd.add(last_signal);
        } else {
          avg = 26.6666666f;
          sd.add_fail();
        }
        last_signal = signal;
        last_sigtime = j;
      }
    }

    c1 *= 1.0f / std::abs(c1);
    c2 *= 1.0f / std::abs(c2);
  }

  return 0;
}


