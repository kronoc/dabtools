/*
This file is part of rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <rtl-sdr.h>
#include <unistd.h>
#include <ctype.h>
#include "dab.h"
#include "input_sdr.h"
#include "input_wf.h"

#define AUTO_GAIN -100
#define DEFAULT_ASYNC_BUF_NUMBER 32

static struct wavefinder_t  wf;
static struct sdr_state_t sdr;
static rtlsdr_dev_t *dev = NULL;

int do_exit = 0;

static pthread_t demod_thread;
static sem_t data_ready;

uint32_t corr_counter;
uint32_t ccount=0;

static void sighandler(int signum)
{
  fprintf(stderr, "Signal caught, exiting!\n");
  do_exit = 1;
  rtlsdr_cancel_async(dev);
}

static void *demod_thread_fn(void *arg)
{
  struct dab_state_t *dab = arg;
  struct sdr_state_t *sdr = dab->device_state;

  while (!do_exit) {
    sem_wait(&data_ready);
    int ok = sdr_demod(&dab->tfs[dab->tfidx], sdr);
    if (ok) {
      dab_process_frame(dab);
    }

    int prev_freq = sdr->frequency;
    if (abs(sdr->coarse_freq_shift) > 1) {
      if (sdr->coarse_freq_shift < 0)
        sdr->frequency = sdr->frequency - 1000;
      else
        sdr->frequency = sdr->frequency + 1000;

      rtlsdr_set_center_freq(dev, sdr->frequency);
    }

    if (abs(sdr->coarse_freq_shift) == 1) {
      if (sdr->coarse_freq_shift < 0)
        sdr->frequency = sdr->frequency - rand() % 1000;
      else
        sdr->frequency = sdr->frequency + rand() % 1000;

      rtlsdr_set_center_freq(dev, sdr->frequency);
    }
    if (abs(sdr->coarse_freq_shift) < 1 && (abs(sdr->fine_freq_shift) > 50)) {
      sdr->frequency = sdr->frequency + (sdr->fine_freq_shift / 3);
      rtlsdr_set_center_freq(dev, sdr->frequency);
    }

    ccount += 1;
    if (ccount == 10) {
      ccount = 0;
    }
  }
  return 0;
}

static void rtlsdr_callback(uint8_t *buf, uint32_t len, void *ctx)
{
  struct sdr_state_t *sdr = ctx;
  int dr_val;
  if (do_exit) {
    return;
  }
  if (!ctx) {
    return;
  }
  memcpy(sdr->input_buffer, buf, len);
  sdr->input_buffer_len = len;
  sem_getvalue(&data_ready, &dr_val);
  if (!dr_val) {
    sem_post(&data_ready);
  }
}

static void eti_callback(uint8_t* eti)
{
  write(1, eti, 6144);
}

// Updated to accept dev_index argument
static int do_sdr_decode(struct dab_state_t* dab, int frequency, int gain, int dev_index)
{
  struct sigaction sigact;
  int32_t device_count;
  int i, r;
  char vendor[256], product[256], serial[256];
  uint32_t samp_rate = 2048000;

  memset(&sdr, 0, sizeof(struct sdr_state_t));

  sdr.frequency = frequency;

  device_count = rtlsdr_get_device_count();
  if (!device_count) {
    fprintf(stderr, "No supported devices found.\n");
    exit(1);
  }

  fprintf(stderr, "Found %d device(s):\n", device_count);
  for (i = 0; i < device_count; i++) {
    rtlsdr_get_device_usb_strings(i, vendor, product, serial);
    fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
  }
  fprintf(stderr, "\n");

  if (dev_index < 0 || dev_index >= device_count) {
    fprintf(stderr, "Invalid device index (%d). Must be between 0 and %d.\n", dev_index, device_count - 1);
    exit(1);
  }
  fprintf(stderr, "Using device %d: %s\n", dev_index, rtlsdr_get_device_name(dev_index));

  r = rtlsdr_open(&dev, dev_index);
  if (r < 0) {
    fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
    exit(1);
  }

  int gains[100];
  int count = rtlsdr_get_tuner_gains(dev, gains);
  fprintf(stderr, "Supported gain values (%d): ", count);
  for (i = 0; i < count; i++)
    fprintf(stderr, "%.1f ", gains[i] / 10.0);
  fprintf(stderr, "\n");

  r = rtlsdr_set_sample_rate(dev, samp_rate);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set sample rate.\n");

  r = rtlsdr_set_center_freq(dev, sdr.frequency);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set center freq.\n");
  else
    fprintf(stderr, "Tuned to %u Hz.\n", sdr.frequency);

  if (gain == AUTO_GAIN) {
    r = rtlsdr_set_tuner_gain_mode(dev, 0);
  } else {
    r = rtlsdr_set_tuner_gain_mode(dev, 1);
    r = rtlsdr_set_tuner_gain(dev, gain);
  }
  if (r != 0) {
    fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
  } else if (gain == AUTO_GAIN) {
    fprintf(stderr, "Tuner gain set to automatic.\n");
  } else {
    fprintf(stderr, "Tuner gain set to %0.2f dB.\n", gain / 10.0);
  }

  r = rtlsdr_reset_buffer(dev);

  sigact.sa_handler = sighandler;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  sigaction(SIGINT, &sigact, NULL);
  sigaction(SIGTERM, &sigact, NULL);
  sigaction(SIGQUIT, &sigact, NULL);
  sigaction(SIGPIPE, &sigact, NULL);

  fprintf(stderr, "Waiting for sync...\n");

  sdr_init(&sdr);
  pthread_create(&demod_thread, NULL, demod_thread_fn, (void *)(dab));
  rtlsdr_read_async(dev, rtlsdr_callback, (void *)(&sdr),
                    DEFAULT_ASYNC_BUF_NUMBER, DEFAULT_BUF_LENGTH);

  if (do_exit) {
    fprintf(stderr, "\nUser cancel, exiting...\n");
  } else {
    fprintf(stderr, "\nLibrary error %d, exiting...\n", r);
  }
  rtlsdr_cancel_async(dev);
  rtlsdr_close(dev);
  return 1;
}

static int do_wf_decode(struct dab_state_t* dab, int frequency)
{
  struct wavefinder_t *wf = dab->device_state;
  int displayed_lock = 0;

  wf_init(wf);
  wf_tune(wf, (frequency + 500) / 1000);

  fprintf(stderr, "Waiting for sync...");

  wf_read_frame(wf, &dab->tfs[0]);
  if ((wf->sync_locked) && (!displayed_lock)) {
    fprintf(stderr, "LOCKED\n");
    displayed_lock = 1;
  }

  while (1) {
    wf_read_frame(wf, &dab->tfs[dab->tfidx]);
    dab_process_frame(dab);
  }
}

// Helper function to check if a string is all digits
int is_digits(const char *str) {
  while (*str) {
    if (!isdigit((unsigned char)*str)) return 0;
    str++;
  }
  return 1;
}

// Helper function to get device index from serial string
int get_rtlsdr_index_by_serial(const char *serial) {
  int32_t device_count = rtlsdr_get_device_count();
  char dev_serial[256];
  for (int i = 0; i < device_count; i++) {
    rtlsdr_get_device_usb_strings(i, NULL, NULL, dev_serial);
    if (dev_serial[0] && strcmp(dev_serial, serial) == 0) {
      return i;
    }
  }
  return -1; // Not found
}

void usage(void)
{
  fprintf(stderr,
    "Usage: dab2eti -f frequency [-g gain] [-d device] [-t device_type]\n"
    "  -f frequency      (required, in Hz)\n"
    "  -g gain           (optional, tuner gain value; default: automatic)\n"
    "  -d device         (optional: for wavefinder, device path; for rtlsdr, device index or serial; default: 0)\n"
    "  -t device_type    (optional: 'wavefinder' or 'rtlsdr'; default: rtlsdr)\n"
    "    For rtlsdr: device can be index (e.g. 0) or serial string (e.g. \"00000001\")\n"
  );
}

int main(int argc, char* argv[])
{
  int frequency = 0;
  int gain = AUTO_GAIN;
  char device[256] = ""; // No default, check per device_type
  char device_type[32] = "rtlsdr"; // Default to rtlsdr
  int device_index = 0;
  int ppm = 0;
  struct dab_state_t* dab;

  int opt;
  while ((opt = getopt(argc, argv, "f:g:d:t:p:h")) != -1) {
    switch (opt) {
      case 'f':
        frequency = atoi(optarg);
        break;
      case 'g':
        gain = atoi(optarg);
        break;
      case 'd':
        strncpy(device, optarg, sizeof(device) - 1);
        device[sizeof(device) - 1] = '\0';
        break;
      case 't':
        if (strcmp(optarg, "wavefinder") == 0 || strcmp(optarg, "rtlsdr") == 0) {
          strncpy(device_type, optarg, sizeof(device_type) - 1);
          device_type[sizeof(device_type) - 1] = '\0';
        } else {
          fprintf(stderr, "Invalid device_type specified. Must be 'wavefinder' or 'rtlsdr'.\n");
          usage();
          return 1;
        }
        break;
      case 'p':
        ppm = atoi(optarg);
      break;
      case 'h':
      default:
        usage();
        return 1;
    }
  }

  if (frequency == 0) {
    usage();
    return 1;
  }

 if (ppm != 0) {
   int freq_orig = frequency;
   frequency = freq_orig + (int)((freq_orig * ppm) / 1000000);
   fprintf(stderr, "Adjusting frequency %d Hz by %d ppm -> %d Hz\n", freq_orig, ppm, frequency);
 }

  if (strcmp(device_type, "wavefinder") == 0) {
    // If device path provided, use it, else default
    const char *wf_device = device[0] ? device : "/dev/wavefinder0";
    if (wf_open(&wf, wf_device) >= 0) {
      init_dab_state(&dab, &wf, eti_callback);
      dab->device_type = DAB_DEVICE_WAVEFINDER;
      do_wf_decode(dab, frequency);
    } else {
      fprintf(stderr, "Failed to open wavefinder device: %s\n", wf_device);
      return 1;
    }
  } else { // rtlsdr
    // Determine index, support serial string!
    if (!device[0]) {
      device_index = 0;
    } else if (is_digits(device)) {
      device_index = atoi(device);
    } else {
      device_index = get_rtlsdr_index_by_serial(device);
      if (device_index < 0) {
        fprintf(stderr, "Could not find RTL-SDR device with serial '%s'\n", device);
        return 1;
      }
    }
    init_dab_state(&dab, &sdr, eti_callback);
    dab->device_type = DAB_DEVICE_RTLSDR;
    do_sdr_decode(dab, frequency, gain, device_index);
  }
}
