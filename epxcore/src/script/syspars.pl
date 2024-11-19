#! /usr/bin/perl
# syspars.pl -- campiona l'oocupazione di CPU e memoria per produrre un file
# 				.csv da elaborare con fogli elettronici
# (C) 2012 GGH Engineering srl per Igea spa.
#

use strict 'vars';
use Getopt::Std;

# Separatore di record e separatore mantissa
my $S     = ";";
my $DOT  = ",";

my $ITER  = 1000000000;
my $DELTA  = 1;

my %opts = ();
getopts("?hn:d:", \%opts);

if ($opts{h} || $opts{'?'}) {
	print STDERR "\nusage: $0 [-h][-n iter][-d secs]\n";
	print STDERR "       -h: print this help\n";
	print STDERR "       -n: specify how many samples (iterations); default: unlimited\n";
	print STDERR "       -d: specify delta between samples in seconds; default: $DELTA\n";

	exit 0;
}

# Voglio il flush di stdout
$| = 1;

$ITER   = $opts{n} if $opts{n};
$DELTA  = $opts{d} if $opts{d};

open(PIPE, "top -b -n $ITER -d $DELTA |") or die "Cant't spawn top";

my $banner = "DATE" . $S .
		"CPU us" . $S . "CPU sy" . $S . "CPU id" . $S . "CPU rem" . $S .
		"MEM tot" . $S . "MEM use" . $S . "MEM fre" . $S . "MEM buf" . $S .
		"SWP tot" . $S . "SWP use" . $S . "SWP fre" . $S . "SWP cac";

my ($date, $cpu, $mem, $swap) = ("", "", "", "");
while (<PIPE>) {

	chomp;
	next unless /^(top)|(Cpu)|(Mem)|(Swap)/;

	s/,//g;

	if (/^top/) {
		if ($banner ne "") {
			print "$banner\n";
			$banner = "";
		}
		else {
			print "$date$S$cpu$S$mem$S$swap\n";
			($date, $cpu, $mem, $swap) = ("", "", "", "");
		}

		my @val = split /\s+/; 
		$date = $val[2];
	}
	elsif (/^Cpu/) {
		my @val = split /\s+/; 
		my $us = $val[1]+0;
		my $sy = $val[2]+0;
		my $id = $val[4]+0;
		my $rem = 100 - $us - $sy - $id;
		$rem = 0 if $rem < 0;

		$cpu = "$us$S$sy$S$id$S$rem";
		$cpu =~ s/\./$DOT/g;
	}
	elsif (/^Mem/) {
		my @val = split /\s+/; 
		my $tot = $val[1]+0;
		my $use = $val[3]+0;
		my $fre = $val[5]+0;
		my $buf = $val[7]+0;

		$mem = "$tot$S$use$S$fre$S$buf";
		$mem =~ s/\./$DOT/g;

	}
	else {	# Swap
		my @val = split /\s+/; 
		my $tot = $val[1]+0;
		my $use = $val[3]+0;
		my $fre = $val[5]+0;
		my $cac = $val[7]+0;

		$swap = "$tot$S$use$S$fre$S$cac";
		$swap =~ s/\./$DOT/g;
	}
}

print "$date$S$cpu$S$mem$S$swap\n";


close(PIPE);
exit 0;
