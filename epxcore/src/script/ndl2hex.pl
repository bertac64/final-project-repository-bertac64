#! /usr/bin/perl
#
#

my %N = (
	0 =>	0x40,
	2 =>	0x02,
	4 =>	0x04,
	6 =>	0x08,
	8 =>	0x10,
	10 =>	0x20,
	12 =>	0x01
);

use strict;

my ($nHV, $pHV, $nLV, $pLV) = (4, 100, 0, 0);

print "pars Hexagon 780 100 1 0 10";

while (<STDIN>) {
	chomp;
	s/^\s*//;
	my ($piu, $meno) = split / *, */, $_;

	my $mpiu = $N{$piu};
	my $mmeno = $N{$meno};
	print " \\\\\n";
	print sprintf("+ $nHV $pHV $nLV $pLV %02X %02X", $mpiu, $mpiu|$mmeno);
}
print "\n";

exit 0;
