#!/usr/bin/perl
use strict;
use warnings;

my $width  = 128;
my $height = 64;
my $filename = "indexed_random.bmp";

# BMP calculations for 8-bit
my $num_colors = 256;
my $palette_size = $num_colors * 4; # Each palette entry is 4 bytes (B, G, R, Reserved)
my $pixel_data_size = $width * $height; 
my $file_size = 54 + $palette_size + $pixel_data_size;

open(my $fh, '>:raw', $filename) or die $!;

# --- FILE HEADER (14 bytes) ---
# Data offset is now 54 (headers) + 1024 (palette) = 1078
print $fh pack("A2 V v v V", 'BM', $file_size, 0, 0, 54 + $palette_size);

# --- INFO HEADER (40 bytes) ---
# Bit count is now 8 instead of 24
print $fh pack("V l2 v2 V6", 40, $width, $height, 1, 8, 0, $pixel_data_size, 0, 0, $num_colors, 0);

# --- COLOR TABLE (1024 bytes) ---
# We define 256 grayscale shades
for (my $i = 0; $i < 256; $i++) {
    # BMP Palette format: Blue, Green, Red, Reserved
    print $fh pack("C4", $i, $i, $i, 0);
}

# --- PIXEL DATA ---
my $padding_len = (4 - ($width % 4)) % 4;
my $padding = "\x00" x $padding_len;

for (my $y = 0; $y < $height; $y++) {
    for (my $x = 0; $x < $width; $x++) {
        # Now we just write a single byte (0-255)
        print $fh pack("C", int(rand(256)));
    }
    print $fh $padding if $padding_len > 0;
}

close($fh);
print "Generated 8-bit indexed BMP: $filename\n";