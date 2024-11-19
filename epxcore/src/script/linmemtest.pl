#!/usr/bin/perl
#
#	epcli -- Perl command to test epcore FPGA memory
#
#	Copyright 2009 by GGH SrL for Igea SpA
#

#use strict;
use Getopt::Std;
use Socket;
use Socket qw(IPPROTO_TCP TCP_NODELAY);

################################################################################

#	Inizializzazione costanti
my $HOST_NAME = "localhost";	# Host di NFCS
my $EPCORE_PORT = 6969;			# Porta del server	

################################################################################

#	Prototipi delle subroutine
sub sendto($);
sub sendtoA($);
sub waitfor($$);

################################################################################

#
#	Main del programma
#

# Abilita l'autoflush
$| = 1;

# Estrae eventuali switch da linea di comando
getopts("dqa:p:h");
my $host       = $opt_a ? $opt_a : $HOST_NAME;
my $port       = $opt_p ? $opt_p : $EPCORE_PORT;

# Controlla che gli argomenti dello script siano corretti
if ($opt_h) {
	print STDERR "\nusage: epcli [-di] [-a host] [-p port]\n";
	print STDERR "       -h print this help\n";
	print STDERR "       -d enable debug output\n";
	print STDERR "       -q quiet mode: dont't print commands/results\n";
	print STDERR "       -a specify server address\t[$HOST_NAME]\n";
	print STDERR "       -p specify server port\n";
	print STDERR "\n";
	exit 1;
}

# Richiede una connessione con il server
print STDERR "+ Connecting to $host:$port\n" if $opt_d;
socket(SERVER, PF_INET, SOCK_STREAM, getprotobyname("tcp")) or die "epcli: $!";
my $host_addr = sockaddr_in($port, inet_aton($host));
connect(SERVER, $host_addr) or die "epcli: $!";

# Identifica la sessione 
my $exit_value = 0;
my $banner     = <SERVER>;
print STDERR "> $banner" unless $opt_q;

#$SIG{INT} = sub { sendto("abort"); 
#				  sleep 1; 
#				  sendto("charge 0 0"); 
#				  waitfor("!CS disch ready");
#				  die };

my $ss = 512;
my $size = sprintf("%04X", $ss);
my $n = 32768;
while (1) {
	my $pattern = 0;
	for (my $bb=0; $bb<1024*1024; $bb+=$ss) {
		my $base = sprintf("%04X", $bb);
		sendto("write $base \\\\") or die "Problems with write";
		for (my $j = 0; $j < $ss; $j+=8) {
			my $b = "+ ";
			for (my $h = 0; $h < 8; $h++) {
				$b .= sprintf("%04X ", $pattern+$j+$h);
			}
			if ($j+8 < $ss) {
				$b .= "\\\\";
				sendto($b) or die "Problems with write";
			}
			else {
				sendtoA($b) or die "Problems with write";
			}
		}

		# leggi il read corrispondente
		sendto("read $base $size") or die "Problems with read";
		while (1) {
			my $line = <SERVER>;
			chomp $line;
			last if ($line =~ /^\+OK/);
	
			my @val = split / /, $line;
			for (my $i=2; $i <=9; $i++) {
				if ($val[$i] != $pattern++) {
					print "Expected $pattern, received $val[$i] (addr=$val[1], i=$i)\n";
					exit 1;
				}
			}
		}
	}

	#sleep 1;
}

exit 0;

#
# Manda un comando e NON attende la risposta. 
#
sub sendto($) 
{
	my $command = shift;

	print STDOUT "< $command\n" unless $opt_q;
	send(SERVER, "$command\r\n", 0) or die "epcli: $!";
}

#
# Manda un comando e attende la risposta. Torna una stringa valida se
# la risposta e` +OK, una stringa vuota se non c'e` match o per timeout.
#
sub sendtoA($) 
{
	my $command = shift;
	my $answer = "";

	sendto($command);

	eval {
		local $SIG{ALRM} = sub { die "alarm clock restart\n" };

		alarm 3;
		$answer = <SERVER>;
		chomp $answer;
		print "ricevuto $answer\n" if $opt_d;
		if ($answer =~ /^\+OK/) {
			print STDOUT "= $answer\n" unless $opt_q;
		}
		else {
			print STDOUT "  $answer\n" unless $opt_q;
			$answer = "";
		}
		alarm 0;
	};
	if ($@ and $@ !~ /alarm clock restart/) {
		$answer = "";
		print STDOUT "- TIMEOUT\n" unless $opt_q;
	}

	return $answer;
}

#
# Cerca un match del pattern passato per al massimo il tempo indicato; 
# torna 1 per OK, 0 per timeout.
#
sub waitfor($$) 
{
	my $pattern = shift;
	my $timeout = shift;
	my $retval = 0;

	eval {
		local $SIG{ALRM} = sub { die "alarm clock restart\n" };

		alarm $timeout;
		while (<SERVER>) {
			chomp;
			print "ricevuto $_\n" if $opt_d;
			print "atteso $pattern\n" if $opt_d;
			if ($_ =~ $pattern) {
				print STDOUT "= $_\n" unless $opt_q;
				alarm 0;
				$retval = 1;
				die;
			}
			else {
				print STDOUT "  $_\n" unless $opt_q;
				alarm $timeout;
			}
		}
	};

	if (!$retval) {
		print STDOUT "- TIMEOUT\n" unless $opt_q;
	}
	return $retval;
}
