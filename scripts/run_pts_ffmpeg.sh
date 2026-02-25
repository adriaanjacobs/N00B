#! /bin/sh

rm /tmp/*.mkv
env PTS_CONCURRENT_TEST_RUNS=1 NUMBER_OF_PROCESSORS=1 ./phoronix-test-suite batch-run local/ffmpeg-noob-full

rm /tmp/*.mkv
env PTS_CONCURRENT_TEST_RUNS=1 NUMBER_OF_PROCESSORS=1 ./phoronix-test-suite batch-run local/ffmpeg-noob-libavformat

rm /tmp/*.mkv
env PTS_CONCURRENT_TEST_RUNS=1 NUMBER_OF_PROCESSORS=1 ./phoronix-test-suite batch-run local/ffmpeg-baseline
