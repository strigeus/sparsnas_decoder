#include <cstdint>
#include <cstdio>
#include <cmath>
#include <complex>
#include <cstring>
#include <mosquitto.h>

// This is the number of pulse per kWh consumed of your elecricity meter.
// In Sweden at least, the standard seems to be 1 pulse per Wh, i.e. 1000 pulses per kWh
int PULSES_PER_KWH=1000;


// These are the last 6 digits from the serial number of the sender.
// The serial number is located under the battery.
// The full serial number looks like "400 666 111"
int SENSOR_ID;

// It seems like different RTL-SDR tune to slightly different frequencies
// Or I'm not really sure what's up, but the 0 and 1 frequencies differ
// between different RTL-SDR and/or sparsnäs. You can have a look at the
// signal in a wave file editor and you can measure the wavelengths of the
// sine waves and put in appropriate values here.
//#define FREQUENCIES {12500.0,50000.0}
float frequencies[2]; //={67500.0,105000.0};

// MQTT connection
struct mosquitto *mosq = NULL;

// MQTT server connection parameters
char MQTT_HOSTNAME[64];
uint32_t MQTT_PORT = 1883;
char MQTT_USERNAME[64];
char MQTT_PASSWORD[64];
char MQTT_TOPIC[64];
char MQTT_CRC_TOPIC[64];

FILE *outfile;
int testing=0;


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

float error_sum;
int error_sum_count;


class SignalDetector {

public:
  SignalDetector() {
    shift_ = 0;
    found_sync_ = 0;
    bits_ = 0;
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
    int bad = false;

    shift_ = 0;
    found_sync_ = 0;
    if (bits_ >= 160) {
      uint16_t crc = crc16(data_, 18);
      uint16_t packet_crc = data_[18] << 8 | data_[19];
      char *m = mesg;

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

      int rcv_sensor_id = dec[5] << 24 | dec[6] << 16 | dec[7] << 8 | dec[8];

      if (data_[0] != 0x11 || data_[1] != (SENSOR_ID & 0xFF) || data_[3] != 0x07 || rcv_sensor_id != SENSOR_ID) {
        bad = true;
        m += sprintf(m, "{\"Bad\":\"");
        for (int i = 0; i < 18; i++)
          m += sprintf(m, "%.2X ", data_[i]);
        m += sprintf(m, "\"");
      } else if (crc == packet_crc) {
        bad = false;
        int seq = (dec[9] << 8 | dec[10]);
        unsigned int effect = (dec[11] << 8 | dec[12]);
        int pulse = (dec[13] << 24 | dec[14] << 16 | dec[15] << 8 | dec[16]);
        int battery = dec[17];
        float watt = effect * 24;
        int data4 = data_[4]^0x0f;
//      Note that data_[4] cycles between 0-3 when you first put in the batterys in t$
        if(data4 == 1){
          watt = (double)((3600000 / PULSES_PER_KWH) * 1024) / (effect);
        }
        m += sprintf(m, "{\"Sequence\": %5d,\"Watt\": %7.2f,\"kWh\": %d.%.3d,\"battery\": %d,\"FreqErr\": %.2f,\"Effect\": %d", seq, watt, pulse/PULSES_PER_KWH, pulse%PULSES_PER_KWH, battery, freq, effect);
        if (testing && crc == packet_crc) {
          error_sum += fabs(freq);
          error_sum_count += 1;
        }
      } else {
        m += sprintf(m, "{\"CRC\": \"ERR\"");
      }

      m += sprintf(m, ",\"Sensor\":%6d}\n", SENSOR_ID);
      char* topic = (crc == packet_crc) ? MQTT_TOPIC : MQTT_CRC_TOPIC;
      if (!testing) {
        if (mosq && !bad) {
          int ret = mosquitto_publish (mosq, NULL, topic, strlen(mesg) - 1, mesg, 0, true);
          if ( ret != MOSQ_ERR_SUCCESS) {
            mosquitto_reconnect(mosq);
            ret = mosquitto_publish (mosq, NULL, topic, strlen(mesg) - 1, mesg, 0, true);
            if (ret != MOSQ_ERR_SUCCESS) {
              fprintf(stderr, "Can't publish to Mosquitto server %d %s\n", ret, mosquitto_strerror(ret) );
              // Tear down the connecton and exit.
              mosquitto_disconnect (mosq);
              mosquitto_destroy (mosq);
              mosquitto_lib_cleanup();
              exit(-1);
            }
          }
        } else
          bad ? fprintf(stderr, "%s", mesg) : printf("%s", mesg);
        if (outfile) {
          fprintf(outfile, "%s", mesg);
          fflush(outfile);
        }
      }
    }
    bits_ = 0;
  }
  uint32_t shift_;
  uint8_t found_sync_;

  uint8_t data_[32];
  uint32_t bits_;
};

void run_for_frequencies(FILE *f, FILE *logfile, float F1, float F2) {
  uint8_t buf[16384];
  SignalDetector sd;

  Complex hist1[27] = { {0} };
  Complex hist2[27] = { {0} };
  Complex sum1 = {0, 0};
  Complex sum2 = {0, 0};

  int hi = 0;

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
}


int run_calibration(FILE *f){
    testing = 1;
    float range_min = -100000, range_max = 100000, step = 5000;
    float invalid_f1 = 1e100, best_f1;

    do {
        float best_error = 1e100;
        best_f1 = invalid_f1;

        for(float f1 = range_min; f1 <= range_max; f1 += step) {
            fseek(f, 0, SEEK_SET);

            fprintf(stderr, "Trying %.0f hz...\n", f1);

            error_sum = 0;
            error_sum_count = 0;
            run_for_frequencies(f, NULL, f1, f1 + 40000.0f);

            if (error_sum_count != 0) {
                float error = error_sum / error_sum_count;
                if (error < best_error) {
                    fprintf(stderr, "Freq %.0f gives error %f\n", f1, error);
                    best_error = error;
                    best_f1 = f1;
                }
            }
        }

        if (best_f1 == invalid_f1) {
            fprintf(stderr, "Nothing found...\n");
            return 1;
        }

        range_min = best_f1 - step * 0.5f;
        range_max = best_f1 + step * 0.5f;
        step /= 10.0f;
    } while (step >= 10.0f);

    fprintf(stderr, "#define FREQUENCIES {%f, %f}\n", best_f1, best_f1 + 40000.0f);
    printf("export SPARSNAS_FREQ_MIN=%f\nexport SPARSNAS_FREQ_MAX=%f\n" , best_f1, best_f1 + 40000.0f);
    return 0;
}


int get_env_int(const char * env_var_name,int * buf) {

    if (const char *env_p = std::getenv(env_var_name))
        if (sscanf(env_p,"%d",buf))
            return 1;
    return 0;
}


int main(int argc, char **argv)
{
    int tmp;
    FILE *f = stdin;
    if (argc >= 2) {
        f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "Failed load!\n");
            return 1;
        }
    }


    //Get SENSOR_ID from environment
    if (get_env_int("SPARSNAS_SENSOR_ID",&tmp))
        SENSOR_ID = tmp;
    else {
        fprintf(stderr, "SPARSNAS_SENSOR_ID not defined or incorrect. Aborting!\n");
        return 1;
    }

    if (argc >= 3 && strcmp(argv[2], "--find-frequencies") == 0)
      return run_calibration(f);


    //Get the parameters from environment
    if (get_env_int("SPARSNAS_PULSES_PER_KWH",&tmp))
        PULSES_PER_KWH = tmp;

    if (get_env_int("SPARSNAS_FREQ_MIN",&tmp))
        frequencies[0] = tmp;
    else {
        fprintf(stderr, "SPARSNAS_FREQ_MIN not defined or incorrect. Aborting!\n");
        return 1;
    }

    if (get_env_int("SPARSNAS_FREQ_MAX",&tmp))
        frequencies[1] = tmp;
    else {
        fprintf(stderr, "SPARSNAS_FREQ_MAX not defined or incorrect. Aborting!\n");
        return 1;
    }

    FILE *logfile;
    if (const char *env_p = std::getenv("SPARSNAS_LOG"))
        logfile=fopen(env_p,"a");
    else
        logfile = NULL;

    memset(MQTT_HOSTNAME, '\0', sizeof(MQTT_HOSTNAME));
    if (const char *env_p = std::getenv("MQTT_HOST"))
      strncpy(MQTT_HOSTNAME, env_p, sizeof(MQTT_HOSTNAME)-1);
    else
      strncpy(MQTT_HOSTNAME, "localhost", sizeof(MQTT_HOSTNAME)-1);

    if (get_env_int("MQTT_PORT",&tmp))
        MQTT_PORT = tmp;

    memset(MQTT_TOPIC, '\0', sizeof(MQTT_TOPIC));
    if (const char *env_p = std::getenv("MQTT_TOPIC"))
      strncpy(MQTT_TOPIC, env_p, sizeof(MQTT_TOPIC)-1);
    else
      sprintf(MQTT_TOPIC, "sparsnas/%d", SENSOR_ID);

    sprintf(MQTT_CRC_TOPIC, "%s/crc", MQTT_TOPIC);

    memset(MQTT_USERNAME, '\0', sizeof(MQTT_USERNAME));
    if (const char *env_p = std::getenv("MQTT_USERNAME"))
      strncpy(MQTT_USERNAME, env_p, sizeof(MQTT_USERNAME)-1);

    memset(MQTT_PASSWORD, '\0', sizeof(MQTT_PASSWORD));
    if (const char *env_p = std::getenv("MQTT_PASSWORD"))
      strncpy(MQTT_PASSWORD, env_p, sizeof(MQTT_PASSWORD)-1);

    // Initialize the Mosquitto library
    mosquitto_lib_init();

    // Create a new Mosquitto runtime instance with a random client ID,
    mosq = mosquitto_new (NULL, true, NULL);
    mosquitto_loop_start(mosq);

    if (mosq) {
      //Set username and password (will be ignored of MQTT_USERNAME=NULL)
      mosquitto_username_pw_set (mosq, MQTT_USERNAME, MQTT_PASSWORD);
      int ret = mosquitto_connect (mosq, MQTT_HOSTNAME, MQTT_PORT, 30);
      if (ret) {
        fprintf (stderr, "Can't connect to Mosquitto server\n");
        mosq = NULL;
      }
    } else
        fprintf (stderr, "Can't initialize Mosquitto library\n");

    //Run the main program
    run_for_frequencies(f, logfile, frequencies[0], frequencies[1]);

    return 0;
}
