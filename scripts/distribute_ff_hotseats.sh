#!/bin/bash
# Distribute FF Hot Seats from latest-wm to all 14 FF members
# This script:
# 1. Creates a folder for each Hot Seats file (without .mp4)
# 2. Copies the .mp4 into that folder
# 3. Copies the folder to all 14 member destinations

# Latest-wm to EGBs member mapping
declare -A MEMBER_MAP=(
    ["Icekkk_20251125_015429"]="3. Icekkk"
    ["sp3nc3_20251123_122648"]="7. sp3nc3"
    ["mehtha_20251124_025932"]="9. mehulthakkar"
    ["mm2024_20251124_235852"]="13. alfie - MM2024"
    ["jpegcollector_20251124_084120"]="14. peterpette"
    ["danki_20251124_003911"]="17. danki"
    ["jkalam_20251122_200439"]="21. jkalam"
    ["CMex_20251123_103018"]="23. CMex"
    ["maxbooks_20251124_064745"]="10. maxbooks"
    ["boris_20251124_103521"]="25. Boris"
    ["downdogcatsup_20251123_220330"]="24. downdogcatsup"
    ["nekondarun_20251123_143353"]="5. nekondarun"
    ["slayer_20251124_220250"]="20. marvizta"
    ["mars_20251124_045327"]="11. mars"
)

# FF Hot Seats files to distribute
HOTSEATS_FILES=(
    "11-11-2025 FF Hot Seats.mp4"
    "11-18-2025 FF Hot Seats.mp4"
)

BASE_PATH="/Alen Sultanic - NHB+ - EGBs"
LATEST_WM="/latest-wm"
STAGING="/Alen Sultanic - NHB+ - EGBs/0.1 Bonuses/Fast Forward⏩/3- Hotseats"

echo "=== FF Hot Seats Distribution Script ==="
echo ""

# Step 1: Create staging folders and copy files into them
echo "Step 1: Creating staging folders..."
for file in "${HOTSEATS_FILES[@]}"; do
    folder_name="${file%.mp4}"  # Remove .mp4 extension
    echo "  Creating folder: $folder_name"
    mega-mkdir "$STAGING/$folder_name" 2>/dev/null || true
done

echo ""
echo "Step 2: Copying files from first available member to staging..."
# Find first member with files and copy to staging
for wm_folder in "${!MEMBER_MAP[@]}"; do
    echo "  Checking $wm_folder..."
    for file in "${HOTSEATS_FILES[@]}"; do
        folder_name="${file%.mp4}"
        source="$LATEST_WM/$wm_folder/$file"
        dest="$STAGING/$folder_name/"

        echo "    Copying: $file -> $dest"
        mega-cp "$source" "$dest" 2>/dev/null && echo "      OK" || echo "      (skipped or exists)"
    done
    break  # Only need to do this once from first member
done

echo ""
echo "Step 3: Distributing to all 14 members..."
for egb_member in "${MEMBER_MAP[@]}"; do
    dest_base="$BASE_PATH/$egb_member/Fast Forward⏩/3- Hotseats"
    echo "  -> $egb_member"

    for file in "${HOTSEATS_FILES[@]}"; do
        folder_name="${file%.mp4}"
        echo "    Copying: $folder_name"
        mega-cp "$STAGING/$folder_name" "$dest_base/" 2>/dev/null && echo "      OK" || echo "      (skipped or exists)"
    done
done

echo ""
echo "=== Distribution Complete ==="
