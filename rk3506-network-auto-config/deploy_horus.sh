#!/bin/bash
# =====================================================================
# Horus Project: Fast Deployment Script
# Purpose: Sync overlay files and binaries to RK3506 board without reflashing
# =====================================================================

# --- Configuration ---
BOARD_IP="192.168.0.100"      # Địa chỉ IP của board
BOARD_USER="root"             # User mặc định
OVERLAY_DIR="./rootfs_overlay" # Thư mục chứa script S45 và cấu hình

echo ">>> Starting Professional Deployment for Horus Project <<<"

# 1. Check Connectivity
echo "[1/4] Checking connection to board ($BOARD_IP)..."
ping -c 1 $BOARD_IP > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "[ERROR] Cannot reach the board. Please check network/IP."
    exit 1
fi

# 2. Sync Network Scripts (S45user_network)
echo "[2/4] Syncing Network Scripts and Configurations..."
scp -r $OVERLAY_DIR/etc/init.d/S45user_network $BOARD_USER@$BOARD_IP:/etc/init.d/
# Phân quyền thực thi ngay trên board
ssh $BOARD_USER@$BOARD_IP "chmod +x /etc/init.d/S45user_network"

# 3. Sync Application Binaries (Optional)
if [ -d "$LOCAL_BIN_DIR" ]; then
    echo "[3/4] Syncing Application Binaries..."
    scp -r $LOCAL_BIN_DIR/* $BOARD_USER@$BOARD_IP:/usr/bin/
    ssh $BOARD_USER@$BOARD_IP "chmod +x /usr/bin/aster /usr/bin/belink 2>/dev/null"
else
    echo "[SKIP] Local bin directory not found. Skipping App sync."
fi

# 4. Trigger Remote Restart
echo "[4/4] Deployment finished. Restarting Network Service on Board..."

ssh $BOARD_USER@$BOARD_IP "/etc/init.d/S45user_network restart"

echo ">>> [SUCCESS] Horus Board is updated and Ready! <<<"
