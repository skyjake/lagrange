#!/bin/sh
# Recreates the iOS asset catalog from the WebP icon sources. Requires dwebp (libwebp).
set -e
cd "$(dirname "$0")"
CAT=Images.xcassets
ICONS=$CAT/AppIcon.appiconset

rm -rf $CAT
mkdir -p $ICONS
for name in Lagrange-square-1024 Lagrange-dark Lagrange-tinted; do
    dwebp -quiet $name.webp -o $ICONS/$name.png
done

cat > $CAT/Contents.json <<EOF
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
EOF

cat > $ICONS/Contents.json <<EOF
{
  "images" : [
    {
      "filename" : "Lagrange-square-1024.png",
      "idiom" : "universal",
      "platform" : "ios",
      "size" : "1024x1024"
    },
    {
      "appearances" : [
        {
          "appearance" : "luminosity",
          "value" : "dark"
        }
      ],
      "filename" : "Lagrange-dark.png",
      "idiom" : "universal",
      "platform" : "ios",
      "size" : "1024x1024"
    },
    {
      "appearances" : [
        {
          "appearance" : "luminosity",
          "value" : "tinted"
        }
      ],
      "filename" : "Lagrange-tinted.png",
      "idiom" : "universal",
      "platform" : "ios",
      "size" : "1024x1024"
    }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
EOF
