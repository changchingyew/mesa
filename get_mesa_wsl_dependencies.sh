# To be run in mesa repository root directory

wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh

sudo apt get install build-essential git gdb valgrind -y
sudo apt-get install llvm -y
sudo apt-get install llvm-11* -y
sudo apt-get install libva* -y
sudo apt-get install ffmpeg -y
sudo apt-get install vlc -y
sudo apt-get install gstreamer-tools -y
sudo apt-get install gstreamer0.10-plugins-good -y
sudo apt-get install libgstreamer0.10-dev -y
sudo apt-get install libgstreamer1.0-0 gstreamer1.0-dev gstreamer1.0-tools gstreamer1.0-doc -y
sudo apt-get install gstreamer1.0-plugins-base gstreamer1.0-plugins-good  -y
sudo apt-get install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly  -y
sudo apt-get install gstreamer1.0-libav -y
sudo apt-get install gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio  -y
sudo apt-get build-dep gstreamer-plugins-base-* -y
sudo apt-get install libgstreamer-plugins-bad1.0-dev -y
sudo apt install git bc build-essential flex bison libssl-dev libelf-dev -y # To compile WSL2 kernel with VGEM
sudo apt install vainfo -y
sudo apt-get build-dep mesa -y
sudo apt-get install libx11-dev -y
sudo apt-get install mesa-common-dev-y
sudo apt-get install libglu1-mesa-dev -y
sudo apt-get install libxrandr-dev -y
sudo apt-get install libxi-dev -y
sudo apt-get install libx11-xcb-dev -y
sudo apt-get install libgles2-mesa -y
sudo apt-get install x11proto-dri2-dev -y
sudo apt-get install x11proto-dri3-dev -y
sudo apt-get install libudev-dev -y

pushd /usr/bin/
sudo rm llvm-config
sudo ln -s llvm-config-11 llvm-config
popd

# To build DRM/EGL support for gstreamer-vaapi
#gstreamer-vaapi/build$ meson build/ -Dwith_drm=yes -Dwith_egl=yes -Dwith_glx=no -Dwith_wayland=no -Dwith_x11=no -Ddebug=true -Doptimization=0

# Setting the environment variables, for automatic setting on each login, please see below for appending into .bashrc
export LIBVA_DRIVERS_PATH=/usr/local/lib/x86_64-linux-gnu/dri
export LIBVA_DRIVER_NAME=d3d12
export VDPAU_DRIVER=d3d12
export GST_VAAPI_ALL_DRIVERS=1
export D3D12_DEBUG=""
export LIBGL_ALWAYS_SOFTWARE=0

# The lines below need to be appended _only once_ to .bashrc
# echo "export LIBVA_DRIVERS_PATH=/usr/local/lib/x86_64-linux-gnu/dri" >> ~/.bashrc
# echo "export LIBVA_DRIVER_NAME=d3d12" >> ~/.bashrc
# echo "export VDPAU_DRIVER=d3d12" >> ~/.bashrc
# echo "export GST_VAAPI_ALL_DRIVERS=1" >> ~/.bashrc
# echo "export D3D12_DEBUG="debuglayer res" >> ~/.bashrc
# echo "export LIBGL_ALWAYS_SOFTWARE=0 >> ~/.bashrc

mkdir build
# to configure mesa for D3D12 Video development
meson build/ -Dgallium-drivers=swrast,d3d12 -Dgallium-va=true -Dgallium-vdpau=true
pushd build/
sudo ninja install
popd

# To test
# playback to screen
    # GST_GL_PLATFORM=egl GST_GL_API=gles2 GST_GL_WINDOW=x11 GST_DEBUG=3 gdbserver :1234 gst-launch-1.0 -v -m filesrc location=INTERFRAMES_inputtranscode_960_540.mp4 ! qtdemux ! h264parse ! vaapih264dec ! vaapisink display=0
# On OpenGL output rendering display
    # GST_GL_PLATFORM=egl GST_GL_API=gles2 GST_GL_WINDOW=x11 GST_DEBUG=3 gdbserver :1234 gst-launch-1.0 -v -m filesrc location=INTERFRAMES_inputtranscode_960_540.mp4 ! qtdemux ! h264parse ! vaapih264dec ! glimagesink
# transcode with HW d3d12 decoder and software x264 encoder 
    # GST_GL_PLATFORM=egl GST_GL_API=gles2 GST_GL_WINDOW=x11 GST_DEBUG=3 gdbserver :1234 gst-launch-1.0 -v -m filesrc location=INTERFRAMES_inputtranscode_960_540.mp4 ! qtdemux ! h264parse ! vaapih264dec ! x264enc tune=zerolatency ! avimux ! filesink location=x264enc_output.mp4
# On filesink
    # GST_GL_PLATFORM=egl GST_GL_API=gles2 GST_GL_WINDOW=x11 GST_DEBUG=3 gdbserver :1234 gst-launch-1.0 -v -m filesrc location=INTERFRAMES_inputtranscode_960_540.mp4 ! qtdemux ! h264parse ! vaapih264dec ! queue ! videoconvert ! pngenc ! multifilesink location="frame%d.png"
