#!/bin/bash
reset

# Set the following regex to filter out printing some errors in output logs
c_GrepErrorExclusions="QueryAdapterInfoCb2|QueryClockCalibration|SetQueuedLimit|D3D12SDKPath"
c_GrepWarningExclusions=""

TextColor_Red='\e[1;31m'
TextColor_Green='\e[1;32m'
TextColor_Blue='\e[1;34m'
TextColor_Yellow='\e[1;33m'

# Argument 1: Input Filename or input directory with *.mp4 files
if test -n "$1"
then

    if [ -d  $1 ]; then
        echo "Input $1 is a directory"
        INPUT_FILES_PATH="$(cd $1 ; pwd)"
        INPUT_FILES="$INPUT_FILES_PATH/*.mp4"
    elif [ -f $1 ]; then
        echo "Input $1 is a file"
        INPUT_FILES="$1"
        INPUT_FILES_PATH="${INPUT_FILES%/*}"
    fi    
else
    INPUT_FILES_PATH="$(cd ~/videoinputs/H264 ; pwd)"
    INPUT_FILES="$INPUT_FILES_PATH/inputtranscode_*.mp4"
fi

# Argument 2: PSNR Threshold
if test -n "$2"
then
    psnrY_Limit=$(($2))
    psnrU_Limit=$(($2))
    psnrV_Limit=$(($2))
    psnrAvg_Limit=$(($2))
    psnrMin_Limit=$(($2))
    psnrMax_Limit=$(($2))
else
    psnrY_Limit=48.0
    psnrU_Limit=48.0
    psnrV_Limit=48.0
    psnrAvg_Limit=48.0
    psnrMin_Limit=48.0
    psnrMax_Limit=48.0
fi

# Argument 3: Decoder to use in gstreamer
# ie. vaapih264dec (Default)
# ie. avdec_h264
if test -n "$3"
then
    GST_DECODER_NAME="$3" 
else
    GST_DECODER_NAME="vaapih264dec" 
fi

# Argument 4: Encoder to use in gstreamer
# ie. x264enc (Default)
# ie. vaapih264enc
if test -n "$4"
then
    GST_ENCODER_NAME="$4" 
else
    GST_ENCODER_NAME="x264enc" 
fi

# Argument 5: Also plays with vaapisink
ENABLE_VAAPISINK_DISPLAY_MODE=0 # Default disabled
if test -n "$5"
then
    if [ "$5" == "vaapisink" ]; then
        ENABLE_VAAPISINK_DISPLAY_MODE=1        
    fi
fi

NUM_INPUT_FILES=$(ls $INPUT_FILES | wc -l)

OUTPUT_FILES_PATH="$INPUT_FILES_PATH/output/"
mkdir -p $OUTPUT_FILES_PATH

CURRENT_TEST_NUMBER=0
for fInput in $INPUT_FILES
do
    fInputFileName="$(basename -- $fInput)"
    CURRENT_TEST_NUMBER=$((CURRENT_TEST_NUMBER+1))
    fOutput="$OUTPUT_FILES_PATH$fInputFileName.output.mp4"
    rm -f "$fOutput"
    fLogOutput="$OUTPUT_FILES_PATH$fInputFileName.logfile.txt"
    rm -f "$fLogOutput"
    echo -e "$TextColor_Blue[Test case $CURRENT_TEST_NUMBER / $NUM_INPUT_FILES]\n\t InputFile: $fInput \n\t OutputFile: $fOutput \n\t LogFile: $fLogOutput"
    echo "Using gstreamer Decoder: $GST_DECODER_NAME"
    echo "Using gstreamer Encoder: $GST_ENCODER_NAME"

    if [ $ENABLE_VAAPISINK_DISPLAY_MODE -eq 1 ];
    then
        echo -e "$TextColor_Blue[Test case $CURRENT_TEST_NUMBER / $NUM_INPUT_FILES] launching gstreamer with vaapisink display=0 for INPUT: $fInput"
        (gst-launch-1.0 -v -m filesrc location="$fInput" ! qtdemux ! h264parse ! vaapih264dec ! vaapisink display=0) > /dev/null 2>&1
    fi    

    if [ "$GST_ENCODER_NAME" == "x264enc" ]; then
        if [ "$D3D12_TEST_USE_GDBSERVER" == "yes" ];       
        then
            echo "Using gdbserver for gstreamer, please connect to port 1234 from gdb..."
            (gdbserver :1234 gst-launch-1.0 -v -m filesrc location="$fInput" ! qtdemux ! h264parse ! $GST_DECODER_NAME ! $GST_ENCODER_NAME qp-max=5 tune=zerolatency ! avimux ! filesink location="$fOutput") > "$fLogOutput" 2>&1    
        else
            (gst-launch-1.0 -v -m filesrc location="$fInput" ! qtdemux ! h264parse ! $GST_DECODER_NAME ! $GST_ENCODER_NAME qp-max=5 tune=zerolatency ! avimux ! filesink location="$fOutput") > "$fLogOutput" 2>&1    
        fi        
    else
        if [ "$D3D12_TEST_USE_GDBSERVER" == "yes" ];       
        then
            echo "Using gdbserver for gstreamer, please connect to port 1234 from gdb..."
            (gdbserver :1234 gst-launch-1.0 -v -m filesrc location="$fInput" ! qtdemux ! h264parse ! $GST_DECODER_NAME ! $GST_ENCODER_NAME ! avimux ! filesink location="$fOutput") > "$fLogOutput" 2>&1    
        else
            (gst-launch-1.0 -v -m filesrc location="$fInput" ! qtdemux ! h264parse ! $GST_DECODER_NAME ! $GST_ENCODER_NAME ! avimux ! filesink location="$fOutput") > "$fLogOutput" 2>&1    
        fi        
    fi
    
    if [ -f "$fOutput" ]; then
        psnrResult=$( (ffmpeg -i $fInput -i $fOutput -filter_complex psnr -f null -) > tmp$fInputFileName.txt 2>&1 ; cat tmp$fInputFileName.txt | grep -i 'Parsed_Psnr'; rm -f tmp$fInputFileName.txt )
        psnrY=$(echo $psnrResult | cut -d : -f 2 | cut -d' ' -f 1)
        psnrY=$(echo "($psnrY+0.5)/1" | bc)
        passPsnrTest=1
        if [ "$((psnrY))" -lt "$((psnrY_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR Y] Failed: $psnrY < $psnrY_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR Y] Passed: $psnrY >= $psnrY_Limit"
        fi
        psnrU=$(echo $psnrResult | cut -d : -f 3 | cut -d' ' -f 1)
        psnrU=$(echo "($psnrU+0.5)/1" | bc)
        if [ "$((psnrU))" -lt "$((psnrU_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR U] Failed: $psnrU < $psnrU_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR U] Passed: $psnrU >= $psnrU_Limit"
        fi
        psnrV=$(echo $psnrResult | cut -d : -f 4 | cut -d' ' -f 1)
        psnrV=$(echo "($psnrV+0.5)/1" | bc)
        if [ "$((psnrV))" -lt "$((psnrV_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR V] Failed: $psnrV < $psnrV_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR V] Passed: $psnrV >= $psnrV_Limit"
        fi
        psnrAvg=$(echo $psnrResult | cut -d : -f 5 | cut -d' ' -f 1)
        psnrAvg=$(echo "($psnrAvg+0.5)/1" | bc)
        if [ "$((psnrAvg))" -lt "$((psnrAvg_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR Avg] Failed: $psnrAvg < $psnrAvg_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR Avg] Passed: $psnrAvg >= $psnrAvg_Limit"
        fi
        psnrMin=$(echo $psnrResult | cut -d : -f 6 | cut -d' ' -f 1)
        psnrMin=$(echo "($psnrMin+0.5)/1" | bc)
        if [ "$((psnrMin))" -lt "$((psnrMin_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR Min] Failed: $psnrMin < $psnrMin_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR Min] Passed: $psnrMin >= $psnrMin_Limit"
        fi
        psnrMax=$(echo $psnrResult | cut -d : -f 7 | cut -d' ' -f 1)
        psnrMax=$(echo "($psnrMax+0.5)/1" | bc)
        if [ "$((psnrMax))" -lt "$((psnrMax_Limit))" ];
        then
            echo -e "$TextColor_Red [Checking PSNR Max] Failed: $psnrMax < $psnrMax_Limit"
            passPsnrTest=0
        else
            echo -e "$TextColor_Green [Checking PSNR Max] Passed: $psnrMax >= $psnrMax_Limit"
        fi

        if [ $passPsnrTest -eq 1 ];
        then
            echo -e "$TextColor_Green \n\t[Test case $CURRENT_TEST_NUMBER] PASSED.\n\n"
        else
            echo -e "$TextColor_Red \n\t[Test case $CURRENT_TEST_NUMBER] FAILED.\n\n"
        fi
        
        errLog=$(cat $fLogOutput | grep -i 'error' | grep -vEwi "$c_GrepErrorExclusions")
        if test -n "$errLog"
        then
            echo -e "$TextColor_Red""See execution log grep -i 'error' | grep -vEwi '$c_GrepErrorExclusions' below:\n$errLog"
        fi

        warnLog=$(cat $fLogOutput | grep -i 'warn' | grep -vEwi "$c_GrepWarningExclusions")
        if test -n "$warnLog"
        then
            echo -e "$TextColor_Yellow""See execution log grep -i 'warn' | grep -vEwi '$c_GrepWarningExclusions' below:\n$warnLog"
        fi
    
    if [ $ENABLE_VAAPISINK_DISPLAY_MODE -eq 1 ];
    then
        echo -e "$TextColor_Blue[Test case $CURRENT_TEST_NUMBER / $NUM_INPUT_FILES] launching gstreamer with vaapisink display=0 for OUTPUT: $fOutput"
        (gst-launch-1.0 -v -m filesrc location="$fOutput" ! qtdemux ! h264parse ! vaapih264dec ! vaapisink display=0) > /dev/null 2>&1
    fi

    else
        echo -e "$TextColor_Red[Error] $fOutput does not exist. Please check $fLogOutput"
    fi
    echo -e "\e[m\n\n-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n\n"
done


# first, check if output file exists
# then compare with ffmpeg below PSNR between input file and transcoded output
# ffmpeg -i input_video.mp4 -i reference_video.mp4 -filter_complex "psnr" -f null /dev/null
# grep/awk the results, have a threshold constant and print PASS/FAIL in red/green

# as apparently there are different code paths, this test should also try displaying to screen with vaapisink display=0 and also with and MPV vdpau 