#!/usr/bin/perl
#
#	epcli -- Perl command to control epcore
#
#	Copyright 2009 by GGH SrL for Igea SpA
#

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
getopts("dD:qa:p:h");
my $host       = $opt_a ? $opt_a : $HOST_NAME;
my $port       = $opt_p ? $opt_p : $EPCORE_PORT;
my $dig        = $opt_D ? $opt_D : 0;

# Controlla che gli argomenti dello script siano corretti
if ($opt_h) {
	print STDERR "\nusage: epcli [-dD] [-a host] [-p port]\n".
					"\t[-S secs]\n";
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

sendtoA("arm") or die "Problems with arm";
waitfor("!CS ready armed", 2);

sleep 1;

sendto("pars Needle $dig 100 1 0 10 \\\\");
sendtoA("+ 1 100 0 10 1 3") or die "Problems with pars";
waitfor("!CS armed wtreat", 2);

sleep 3;

sendto("pulse");
waitfor("!CS trtmt done", 2);

sleep 3;

sendtoA("get #1") or die "Problems with get";
waitfor("!CS done ready", 2);

print "DL=$dig\n";
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

