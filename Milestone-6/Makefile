# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Makefile for nbt-aad (QNX Neutrino ARMv7LE)
# Target: Cortex-A15 Little Endian (BMW NBT Evo)

QCC ?= qcc
TARGET_ARCH ?= -Vgcc_ntoarmv7le_cpp

CXX = $(QCC) $(TARGET_ARCH)
CC  = $(QCC) -Vgcc_ntoarmv7le

INCLUDES = -I./include -I./src
# Remove -fPIC and -Bdynamic to produce a true QNX executable (ET_EXEC), not a shared lib (ET_DYN)
# -Wc,-include,cstddef/cstdint: QNX 6.5 headers + modern GCC (podman 4.9 image) don't agree on size_t/uintN_t.
CXXFLAGS = -O2 -Wall -D_QNX_SOURCE -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -Wc,-include,cstddef -Wc,-include,cstdint $(INCLUDES)
# Set RPATH so QNX's dynamic linker automatically finds liba2i_client.so in /opt/mm/lib without copying.
# libssl/libcrypto by explicit path (not -lssl -lcrypto): the QNX gcc spec searches the sysroot libdir
# before -L./lib, and the old sysroot OpenSSL doesn't export TLSv1_2_client_method.
LDFLAGS  = -L./lib -la2i_client -lsocket ./lib/libssl.so ./lib/libcrypto.so -lscreen -lang-c++ -Wl,-rpath,/opt/mm/lib:/lib:/usr/lib

SRCS = src/main.cpp \
       src/audio_manager.cpp \
       src/a2i_stream.cpp \
       src/audio_route.cpp \
       src/pcm_utils.cpp \
       src/video_manager.cpp \
       src/tcp_server.cpp \
       src/aa_session.cpp \
       src/aa_cryptor.cpp \
       src/bluetooth_manager.cpp \
       src/wireless_setup.cpp \
       src/touch_probe.cpp \
       src/dsi_client.cpp \
       src/voice_scenario.cpp \
       src/ccs_input.cpp \
       src/sdr_builder.cpp \
       src/crash_handler.cpp

BUILD_DIR = build
# Object files go into build/ (mirrors src/ basenames), not next to the sources.
OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = nbt-aad

.PHONY: all clean

all: $(TARGET) test_display test_controls plugin rhmiapp

$(TARGET): $(OBJS)
	@echo [LINK] $@
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	@echo [DONE] Built $(TARGET) for QNX armv7le

test_display: test_display.cpp
	@echo [CXX] test_display.cpp
	$(CXX) $(CXXFLAGS) -o test_display test_display.cpp -L./lib -lscreen -lang-c++
	@echo [DONE] Built test_display for QNX armv7le

test_controls: test_controls.cpp src/dsi_client.cpp src/dsi_client.hpp src/most_msg.hpp src/ctl_wizard.hpp src/ccs_decoder.hpp
	@echo [CXX] test_controls.cpp
	$(CXX) $(CXXFLAGS) -o test_controls test_controls.cpp src/dsi_client.cpp
	@echo [DONE] Built test_controls for QNX armv7le

# ---- HmiMain3D plugin (plugin/): GNU libstdc++ (_gpp variant) to match BMW's plugins, PIC shared object ----
PLUGIN_CXX      = $(QCC) -Vgcc_ntoarmv7le_gpp
PLUGIN_CXXFLAGS = -O2 -Wall -D_QNX_SOURCE -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -fPIC -I./plugin
# rhmiapp/nbt_aad_app.cpp pulls <spawn.h> -> sys/types.h, whose size_t typedef miscompiles on QNX 6.5
# unless cstddef/cstdint are force-included first; those headers in turn need C++11. Kept local to the
# rhmiapp recipe so the plain-C++ plugin build (libhmiaad.so) is unaffected.
RHMI_CXXFLAGS   = $(PLUGIN_CXXFLAGS) -Wc,-std=gnu++0x -Wc,-include,cstddef -Wc,-include,cstdint
PLUGIN_RPATH    = -Wl,-rpath,/lib:/usr/lib:/fs/sda0/repository/istep/lib:/opt/hmi/ID5/lib

# ---- native iDrive PluginApp (rhmiapp/): libNbtAad.so + manifest/XML/DBs -> /opt/hmi/ID5/data/ro/Plugins/NbtAad ----
# RHMI_LIBDIR: copies of the car's libAppFramework.A.so and libCoreFramework.A.so (opt/hmi/ID5/lib in the dump).
RHMI_LIBDIR ?= ./rhmiapp/lib
RHMI_LIBS = $(wildcard $(RHMI_LIBDIR)/libAppFramework.A.so $(RHMI_LIBDIR)/libCoreFramework.A.so)

.PHONY: rhmiapp
rhmiapp: rhmiapp/libNbtAad.so

rhmiapp/libNbtAad.so: rhmiapp/nbt_aad_app.cpp
	@echo [BUILD] PluginApp $@
	$(PLUGIN_CXX) $(RHMI_CXXFLAGS) -shared -Wl,-soname,libNbtAad.so -o $@ rhmiapp/nbt_aad_app.cpp $(RHMI_LIBS) -Wl,--allow-shlib-undefined -lang-c++
	@echo [DONE] Built $@ - install with NbtAad.manifest, NbtAad.xml, NbtAad_ALL_*.zip into Plugins/NbtAad

.PHONY: plugin
plugin: plugin/libhmiaad.so plugin/plugin_check rhmiapp/libNbtAad.so

PLUGIN_SRCS = plugin/nbt_aad_plugin.cpp plugin/hmi_services.cpp plugin/hmi_menu.cpp

plugin/libhmiaad.so: $(PLUGIN_SRCS) plugin/plugin_abi.hpp plugin/hmi_services.hpp plugin/hmi_menu.hpp
	@echo [CXX] $@
	$(PLUGIN_CXX) $(PLUGIN_CXXFLAGS) -shared -Wl,-soname,libhmiaad.so -o $@ $(PLUGIN_SRCS) -lang-c++
	@echo [DONE] Built $@ - install next to plugin/NbtAadPlugin.xml in hmiplugins

plugin/plugin_check: plugin/plugin_check.cpp plugin/plugin_abi.hpp
	@echo [CXX] $@
	$(PLUGIN_CXX) -O2 -Wall -D_QNX_SOURCE -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -I./plugin -o $@ plugin/plugin_check.cpp -lang-c++ $(PLUGIN_RPATH)
	@echo [DONE] Built $@

# Compile src/<name>.cpp -> build/<name>.o (order-only dep creates the dir)
$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	@echo [CXX] $<
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@echo [CLEAN] Removing build directory and binaries
	rm -rf $(BUILD_DIR) $(TARGET) test_display test_controls plugin/libhmiaad.so plugin/plugin_check rhmiapp/libNbtAad.so
