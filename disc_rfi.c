#include "disc_rfi.h"
#include "jsmn.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

uint64_t rfi_headerlen = 0;

// Header metadata
int rfi_tracks = 0;
int rfi_sides = 0;
long rfi_rate = 0;
unsigned char rfi_writeable = 0;

uint32_t rfi_bytes_written;
uint32_t rfi_pulses;
uint8_t rfi_pulsecount;

float
rfi_samplestous(const long samples) {
  return (((float)1 / (((float)rfi_rate) / (float)USINSECOND)) * (float)samples);
}

void
rfi_addbit(uint32_t* p_pulses, char bit) {
  rfi_pulses = ((rfi_pulses << 1) | bit);
  rfi_pulsecount++;

  if (rfi_pulsecount == 32) {

    if (rfi_bytes_written < k_disc_max_bytes_per_track)
      p_pulses[rfi_bytes_written++] = rfi_pulses;

    rfi_pulsecount = 0;
    rfi_pulses = 0;
  }
}

uint32_t
disc_rfi_readtrack(struct disc_struct* p_disc, const int track, const int side, char* buf, const uint32_t buflen) {
  struct util_file* p_file = disc_get_file(p_disc);
  int rfi_track;
  int rfi_side;
  float rfi_rpm;
  char rfi_trackencoding[10];
  unsigned long rfi_trackdatalen;
  char metabuffer[1024];
  
  // Check for valid file handle
  assert(p_file != NULL);

  // Make sure we have already found valid JSON metadata
  if (rfi_headerlen == 0)
    return 0;
    
  // Seek past file JSON metadata
  util_file_seek(p_file, rfi_headerlen + 3);
  
  while (!util_file_feof(p_file)) {
    jsmn_parser parser;
    int numtokens;
    uint64_t metapos;
    int i;
    
    // Initialise track metadata
    rfi_track = -1;
    rfi_side = -1;
    rfi_rpm = -1;
    rfi_trackencoding[0] = 0;
    rfi_trackdatalen = 0;

    // Read track metadata
    metapos = util_file_get_pos(p_file);
    
    if (util_file_read(p_file, metabuffer, sizeof(metabuffer)) != sizeof(metabuffer))
      return 0;

    util_file_seek(p_file, metapos);

    if (metabuffer[0] != '{')
      return 0;

    for (i = 0; i < (int)sizeof(metabuffer); i++) {
      if ((metabuffer[i] == '}') && ((i+1) < (int)sizeof(metabuffer)))
      {
        metabuffer[i+1]=0;
        break;
      }
    }

    // Quick check for validty and to count the tokens
    jsmn_init(&parser);
    numtokens = jsmn_parse(&parser, metabuffer, sizeof(metabuffer), NULL, 0);

    if (numtokens>0)
    {
      jsmntok_t *tokens;

      tokens = util_malloc(numtokens*sizeof(jsmntok_t));

      if (tokens == NULL) return 0;

      jsmn_init(&parser);
      numtokens = jsmn_parse(&parser, metabuffer, sizeof(metabuffer), tokens, numtokens);

      // Move file pointer to first byte after track header
      util_file_seek(p_file, metapos + tokens[0].end);

      for (i = 0; i < numtokens; i++)
      {
        char rfic;

        if ((tokens[i].type == JSMN_PRIMITIVE) && (tokens[i].size == 1) && ((i+1) <= numtokens)) {
          if (strncmp(&metabuffer[tokens[i].start], "enc", tokens[i].end-tokens[i].start) == 0) {
            rfic = metabuffer[tokens[i+1].end];
            metabuffer[tokens[i+1].end] = 0;

            if (strlen(&metabuffer[tokens[i+1].start]) < sizeof(rfi_trackencoding))
              strcpy(rfi_trackencoding, &metabuffer[tokens[i+1].start]);

            metabuffer[tokens[i+1].end] = rfic;
          }
          else
          if (strncmp(&metabuffer[tokens[i].start], "track", tokens[i].end-tokens[i].start) == 0) {
            rfic = metabuffer[tokens[i+1].end];
            metabuffer[tokens[i+1].end] = 0;

            sscanf(&metabuffer[tokens[i+1].start], "%3d", &rfi_track);

            metabuffer[tokens[i+1].end] = rfic;
          }
          else
          if (strncmp(&metabuffer[tokens[i].start], "side", tokens[i].end-tokens[i].start)==0) {
            rfic = metabuffer[tokens[i+1].end];
            metabuffer[tokens[i+1].end] = 0;

            sscanf(&metabuffer[tokens[i+1].start], "%1d", &rfi_side);

            metabuffer[tokens[i+1].end] = rfic;
          }
          else
          if (strncmp(&metabuffer[tokens[i].start], "len", tokens[i].end-tokens[i].start) == 0) {
            rfic = metabuffer[tokens[i+1].end];
            metabuffer[tokens[i+1].end] = 0;

            sscanf(&metabuffer[tokens[i+1].start], "%8lu", &rfi_trackdatalen);

            metabuffer[tokens[i+1].end] = rfic;
          }
          else
          if (strncmp(&metabuffer[tokens[i].start], "rpm", tokens[i].end-tokens[i].start)==0) {
            rfic = metabuffer[tokens[i+1].end];
            metabuffer[tokens[i+1].end] = 0;

            sscanf(&metabuffer[tokens[i+1].start], "%f", &rfi_rpm);

            metabuffer[tokens[i+1].end] = rfic;
          }
        }
      }

      util_free(tokens);
      tokens=NULL;

      // Is this the track we want?
      if ((rfi_track == track) && (rfi_side == side) && (rfi_trackencoding[0] != 0) && (rfi_trackdatalen != 0)) {
        //log_do_log(k_log_disc, k_log_info, "rfi track : %s", metabuffer);

        // Make sure we have an RPM
        if (rfi_rpm == -1)
          rfi_rpm = 300.0;

        // Make sure we have a sample rate
        if (rfi_rate == -1)
          rfi_rate = 12500000;

        if (strstr(rfi_trackencoding, "raw") != NULL) {
          if (rfi_trackdatalen <= buflen) {
            util_file_read(p_file, buf, rfi_trackdatalen);

            return rfi_trackdatalen;
          } else {
            util_file_read(p_file, buf, buflen);

            return buflen;
          }
        }
        else
        if (strstr(rfi_trackencoding, "rle") != NULL)
        {
          unsigned char c, b, blen, s;
          long rlen = 0;
          char *rlebuff;

          rlebuff = util_malloc(rfi_trackdatalen);

          if (rlebuff == NULL) return 0;

          blen = 0;
          s = 0;
          b = 0;

          util_file_read(p_file, rlebuff, rfi_trackdatalen);

          for (i = 0; (unsigned int)i < rfi_trackdatalen; i++)
          {
            // Extract next RLE value
            c = rlebuff[i];

            while (c > 0)
            {
              b = (b << 1) | s;
              blen++;

              if (blen == 8)
              {
                buf[rlen++] = b;

                // Check for unpacking overflow
                if (rlen >= buflen)
                {
                  util_free(rlebuff);

                  return rlen;
                }

                b = 0;
                blen = 0;
              }

              c--;
            }

            // Switch states
            s = 1-s;
          }

          util_free(rlebuff);

          return rlen;
        }
      } else {
        // If the currently read track is more than the one we want, then the track isn't here
        if (rfi_track > track) return 0;

        // Skip this track, it's not the one we want
        util_file_seek(p_file,  util_file_get_pos(p_file) + rfi_trackdatalen);
      }
    }
    else // No tokens found, give up further processing
      return 0;
  }

  return 0;
}

void
disc_rfi_load(struct disc_struct* p_disc) {
  /* https://github.com/picosonic/bbc-fdc/blob/master/tools/rfi.h
   */

  struct util_file* p_file = disc_get_file(p_disc);
  unsigned char buff[4];
  char *rfi_headerstring = NULL;
  int i_track;
  int i_side;
  char *rfi_samples = NULL;
  uint32_t rfi_sampleslen = (1024 * 1024 * 5);
  uint32_t rfi_sampled = 0;

  // Check for valid file handle
  assert(p_file != NULL);

  // Check for RFI magic
  if (util_file_read(p_file, buff, 3) != 3) {
    util_bail("rfi file no header");
  }

  buff[3]=0;
  if (strcmp((char *)buff, RFI_MAGIC) != 0) {
    util_bail("rfi file not valid");
  }

  // Check for JSON open bracket
  if (util_file_read(p_file, buff, 1) != 1) {
    util_bail("rfi file missing metadata");
  }

  if (buff[0] != '{') {
    util_bail("rfi file not valid");
  }

  rfi_samples = util_malloc(rfi_sampleslen);

  if (rfi_samples == NULL) {
    util_bail("rfi failed to allocate samples buffer");
  }

  // Parse the rest of the JSON metadata
  while (!util_file_feof(p_file)) {
    if (util_file_read(p_file, buff, 1) != 1) {
      util_bail("rfi incomplete metadata");
    }

    // Check for end of JSON metadata
    if (buff[0] == '}') {
      jsmn_parser parser;
      int numtokens;

      rfi_headerlen = util_file_get_pos(p_file) - 3;

      rfi_headerstring = util_malloc(rfi_headerlen + 1);

      if (rfi_headerstring == NULL) {
        util_bail("rfi failed to allocate metadata buffer");
      }

      // Read the whole JSON metadata string
      util_file_seek(p_file, 3);
      util_file_read(p_file, rfi_headerstring, rfi_headerlen);
      rfi_headerstring[rfi_headerlen] = 0;

      log_do_log(k_log_disc, k_log_info, "rfi header : %s", rfi_headerstring);

      // Parse JSON metadata
      jsmn_init(&parser);

      // Quick check for validity and to count the tokens
      numtokens=jsmn_parse(&parser, rfi_headerstring, rfi_headerlen, NULL, 0);

      if (numtokens>0) {
        int i;
        jsmntok_t *tokens;

        tokens = util_malloc(numtokens*sizeof(jsmntok_t));

        if (tokens == NULL) {
          util_free(rfi_headerstring);
          util_bail("rfi failed to allocate parser");
        }

        // Full parse of JSON metadata
        jsmn_init(&parser);
        numtokens = jsmn_parse(&parser, rfi_headerstring, rfi_headerlen, tokens, numtokens);

        for (i = 0; i < numtokens; i++) {
          if ((tokens[i].type == JSMN_PRIMITIVE) && (tokens[i].size == 1) && ((i+1) <= numtokens))
          {
            char rfic;

            if (strncmp(&rfi_headerstring[tokens[i].start], "tracks", tokens[i].end-tokens[i].start) == 0)
            {
              rfic=rfi_headerstring[tokens[i+1].end];
              rfi_headerstring[tokens[i+1].end] = 0;

              sscanf(&rfi_headerstring[tokens[i+1].start], "%3d", &rfi_tracks);

              rfi_headerstring[tokens[i+1].end] = rfic;
            }
            else
            if (strncmp(&rfi_headerstring[tokens[i].start], "sides", tokens[i].end-tokens[i].start) == 0)
            {
              rfic=rfi_headerstring[tokens[i+1].end];
              rfi_headerstring[tokens[i+1].end] = 0;

              sscanf(&rfi_headerstring[tokens[i+1].start], "%1d", &rfi_sides);

              rfi_headerstring[tokens[i+1].end] = rfic;
            }
            else
            if (strncmp(&rfi_headerstring[tokens[i].start], "rate", tokens[i].end-tokens[i].start) == 0)
            {
              rfic=rfi_headerstring[tokens[i+1].end];
              rfi_headerstring[tokens[i+1].end] = 0;

              sscanf(&rfi_headerstring[tokens[i+1].start], "%10ld", &rfi_rate);

              rfi_headerstring[tokens[i+1].end] = rfic;
            }
            else
            if (strncmp(&rfi_headerstring[tokens[i].start], "writeable", tokens[i].end-tokens[i].start) == 0)
            {
              rfic=rfi_headerstring[tokens[i+1].end];
              rfi_headerstring[tokens[i+1].end] = 0;

              sscanf(&rfi_headerstring[tokens[i+1].start], "%1c", &rfi_writeable);

              if (rfi_writeable == '1')
                rfi_writeable = 1;
              else
                rfi_writeable = 0;

              rfi_headerstring[tokens[i+1].end] = rfic;
            }
          }
        }

        util_free(tokens);
        tokens = NULL;
        util_free(rfi_headerstring);

        disc_set_is_double_sided(p_disc, rfi_sides > 0 ? 1 : 0);

        // We're done reading the header, now read the tracks
        for (i_track = 0; i_track < rfi_tracks; i_track++) {
          for (i_side =0; i_side < rfi_sides; i_side++) {
            rfi_sampled =  disc_rfi_readtrack(p_disc, i_track, i_side, rfi_samples, rfi_sampleslen);

            rfi_bytes_written = 0;

            if (rfi_sampled > 0) {
              uint32_t* p_pulses;
              uint32_t datapos;
              uint32_t count;
              uint8_t c, j;
              int8_t level;
              int8_t bi;

              p_pulses = disc_get_raw_pulses_buffer(p_disc, i_side, i_track); // @ 250 Kbit/s (4us)
              rfi_pulses = 0;

              // Set up the sampler
              level = (rfi_samples[0] & 0x80) >> 7;
              bi = level;
              count = 0;
              rfi_pulsecount = 0;

              for (datapos = 0; datapos < rfi_sampled; datapos++) {
                // Extract byte from buffer
                c = rfi_samples[datapos];

                // Process each bit of the extracted byte
                for (j = 0; j < 8; j++) {
                  // Determine next level
                  bi = ((c & 0x80) >> 7);

                  // Increment samples counter
                  count++;

                  // Look for level changes
                  if (bi != level) {
                    // Flip level cache
                    level = 1-level;

                    // Look for rising edge
                    if (level == 1) {

                      if (rfi_samplestous(count) < 6.0) { // Threshold for "1" or "01"
                        rfi_addbit(p_pulses, 1);
                      } else {
                        rfi_addbit(p_pulses, 0);
                        rfi_addbit(p_pulses, 1);
                      }

                      // Reset samples counter
                      count = 0;
                    }
                  }

                  // Move on to next sample level (bit)
                  c = c << 1;
                }
              }
            }

            disc_set_track_length(p_disc, i_side, i_track, rfi_bytes_written);
          }
        }

        return;
      } else {
        util_free(rfi_headerstring);
        util_bail("rfi error parsing metadata");
      }

      break;
    }
  }
}
