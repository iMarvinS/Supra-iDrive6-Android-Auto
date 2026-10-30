#!/bin/sh
# hu-bootstrap.sh - idempotent head-unit wireless bringup, run by the NbtAad iDrive plugin at startup
# (libNbtAad.so -> runHuBootstrap()) when NBT_HU_BOOTSTRAP is on. It is the self-contained, re-runnable
# version of the remote block in aa-supra/tools/hu-up.sh:
#   1. uap0 IP 172.16.224.1/24
#   2. Marvell micro-AP (uaptool up)            -> SSID BMW34567 CarPlay
#   3. DHCP for uap0 (second DnsMasq instance, dnsmasq-aa)
#   4. CarPlay mm-renderer (IVA-HD decoder, provides /dev/name/local/mdev for the projection video)
# It does NOT start nbt-aad: the plugin/daemon owns that (launched on the "Start Android Auto" menu row).
#
# Idempotent: safe to run on every boot and more than once. Each step checks before acting, so a second
# run neither disrupts an active AP/DHCP lease nor spawns a second mm-renderer. This is needed on
# NON-CarPlay head units, where these CarPlay components ship on the image but are never started. On a
# CarPlay-equipped unit the stock CarPlay stack already brings all of this up, so NBT_HU_BOOTSTRAP stays
# off there and this never runs. All output -> stdout (the plugin redirects it to /dev/shmem/hu-bootstrap.log).
#
# Overridable from the environment (defaults match hu-up.sh):
#   NBT_AP_IF   uap0                 micro-AP interface
#   NBT_AP_IP   172.16.224.1         AP IP (and DHCP gateway/option 3)
#   UAPTOOL     /opt/aa/uaptool      Marvell uaptool binary (install it next to nbt-aad)

PATH=/bin:/usr/bin:/sbin:/usr/sbin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/opt/conn/bin
export PATH

IF="${NBT_AP_IF:-uap0}"
AP_IP="${NBT_AP_IP:-172.16.224.1}"
UAPTOOL="${UAPTOOL:-/opt/aa/uaptool}"

log() { echo "[hu-bootstrap] $*"; }

log "start (IF=$IF AP_IP=$AP_IP uaptool=$UAPTOOL)"

# 1. uap0 IP ---------------------------------------------------------------------------------------------
if ifconfig "$IF" 2>/dev/null | grep -q "$AP_IP"; then
	log "uap0 IP $AP_IP already set"
else
	log "set uap0 IP $AP_IP"
	ifconfig "$IF" "$AP_IP" netmask 255.255.255.0
fi

# 2. Marvell micro-AP (uaptool up) -----------------------------------------------------------------------
# uaptool reconfigures and restarts the AP each time, so run it at most once per boot: a sentinel in
# /dev/shmem (cleared by a reboot) plus a live-IP check decides. Missing uaptool is a warning, not fatal -
# DHCP/renderer still get set up (useful when the AP is already up by other means).
AP_SENTINEL=/dev/shmem/aa-ap.up
if [ -f "$AP_SENTINEL" ] && ifconfig "$IF" 2>/dev/null | grep -q "$AP_IP"; then
	log "micro-AP already brought up this boot (sentinel $AP_SENTINEL)"
elif [ ! -x "$UAPTOOL" ]; then
	log "WARN: $UAPTOOL missing/not executable - skipping micro-AP bring-up (install uaptool in /opt/aa)"
else
	log "bring up micro-AP (uaptool up $IF)"
	if "$UAPTOOL" up "$IF"; then
		: > "$AP_SENTINEL"
		log "micro-AP up"
	else
		log "WARN: uaptool up failed"
	fi
fi

# 3. DHCP (dnsmasq-aa) -----------------------------------------------------------------------------------
# Only (re)start when it is not already running, so a re-run keeps active leases. Our instance is the one
# started with the dnsmasq-aa.conf below (detected via pidin, like mm-renderer - the HU shell has no
# reliable `kill -0`). The conf is rewritten each time it does start (cheap, keeps it in sync with AP_IP).
PIDF=/dev/shmem/dnsmasq-aa.pid
if pidin ar 2>/dev/null | grep -q "[d]nsmasq-aa.conf"; then
	log "dnsmasq-aa already running"
else
	[ -f "$PIDF" ] && kill "$(cat "$PIDF" 2>/dev/null)" 2>/dev/null
	CONF=/dev/shmem/dnsmasq-aa.conf
	{
		echo "port=0"
		echo "interface=$IF"
		echo "bind-interfaces"
		echo "dhcp-range=172.16.224.50,172.16.224.150,255.255.255.0,12h"
		echo "dhcp-option=3,$AP_IP"
		echo "dhcp-authoritative"
		echo "dhcp-leasefile=/dev/shmem/dnsmasq-aa.leases"
	} > "$CONF"
	if [ -x /opt/conn/bin/DnsMasq ]; then
		log "start dnsmasq-aa"
		/opt/conn/bin/DnsMasq -8 /dev/console -u root -C "$CONF" -x "$PIDF"
	else
		log "WARN: /opt/conn/bin/DnsMasq missing - no DHCP for $IF"
	fi
fi

# 4. CarPlay mm-renderer ---------------------------------------------------------------------------------
# Provides mm-renderer + the IVA-HD hardware decoder (/dev/name/local/mdev) the projection video needs.
if pidin ar 2>/dev/null | grep -q "[c]arplay/mm-renderer"; then
	log "mm-renderer already running"
else
	DEC=/opt/conn/lib/carplay/mm/ivahd_video_decoder-omap5430-dce3.so
	[ -x /opt/conn/bin/carplay/mm-renderer ] || log "WARN: /opt/conn/bin/carplay/mm-renderer missing"
	[ -f "$DEC" ] || log "WARN: $DEC missing"
	log "start CarPlay mm-renderer"
	LD_LIBRARY_PATH=/opt/conn/lib/carplay:/opt/conn/lib/carplay/mm:/opt/conn/lib/carplay/mmedia:/boot/lib:/lib:/usr/lib:/opt/os/lib:/opt/sys/lib:/opt/core/lib:/opt/car/lib:/lib/graphics/omap5430 \
		MM_RENDERER_CONFIG=/opt/conn/etc/carplay/mm-renderer.conf \
		MM_PLAYLIST_CONFIG=/opt/conn/etc/carplay/mm-playlist.conf \
		MM_INIT=/opt/conn/lib/carplay/mm \
		LD_PRELOAD="$DEC" \
		/opt/conn/bin/carplay/mm-renderer -vvvv -r /pps/services/multimedia/renderer >/dev/shmem/mmr-aa.log 2>&1 &
	sleep 2
	if pidin ar 2>/dev/null | grep -q "[c]arplay/mm-renderer"; then
		log "mm-renderer started"
	else
		log "WARN: mm-renderer did not come up - see /dev/shmem/mmr-aa.log"
	fi
fi

log "done"
