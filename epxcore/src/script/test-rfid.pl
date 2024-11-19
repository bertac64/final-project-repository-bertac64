#!/usr/bin/perl
#
#	test-rfid -- Perl command for test of RFID transceiver
#
#	Copyright 2011 by GGH srl for Igea spa
#

use Getopt::Std;
use Socket;
use Socket qw(IPPROTO_TCP TCP_NODELAY);

################################################################################

#	Inizializzazione costanti
my $PATIENT_ID = "12345678";		# Patient ID digest
my $HOST_NAME = "localhost";		# Host di NFCS
my $EPCORE_PORT = 6969;				# Porta del server	
my $BINDIR = "/opt/eps02/bin";		# Directory dei binari
my $CONFDIR = "/opt/eps02/conf";	# Directory dei file di configurazione

################################################################################

#	Prototipi delle subroutine
sub sendto($);
sub sendtoA($);
sub waitfor($$);
sub msleep($);
sub log_out($);
sub log_ko($);

################################################################################

#
#	Main del programma
#

# Definisci il nome della stringa trovata
my $Found = "";
my $Alarm = "";

# Abilita l'autoflush
$| = 1;

# Estrae eventuali switch da linea di comando
getopts("dqa:p:hN:");
my $host       = $opt_a ? $opt_a : $HOST_NAME;
my $port       = $opt_p ? $opt_p : $EPCORE_PORT;
my $n_of_pass  = $opt_N ? $opt_N : 1000000000;

$BINDIR = $ENV{BINDIR} if $ENV{BINDIR};			# Se definite da shell, usa
$CONFDIR = $ENV{CONFDIR} if $ENV{CONFDIR};		# quelle (debug only)

# Controlla che gli argomenti dello script siano corretti
if ($opt_h) {
	print STDERR "\nusage: test-rfid [-dqh] [-N rounds] [-a host] [-p port]\n";
	print STDERR "       -h print this help\n";
	print STDERR "       -d enable debug output\n";
	print STDERR "       -q quiet mode: dont't print commands/results\n";
	print STDERR "       -a specify server address\t[$HOST_NAME]\n";
	print STDERR "       -p specify server port\n";
	print STDERR "       -N specify number of pass (default: forever)\n";
	print STDERR "\n";
	exit 1;
}

open(LOG, '>>./burn-in.log');

# Bannerino di apertura
log_out("Starting a new RFID sequence test ============================== ");

# Ferma il server
print STDERR "* Stopping epcore\n" unless $opt_q;
system "killall", "epcore";
sleep 3;
	
# Fai partire il server
print STDERR "* Starting epcore\n" unless $opt_q;
system "$BINDIR/epcore -p $CONFDIR/epcore.properties &";
sleep 2;

# Richiede una connessione con il server
print STDERR "+ Connecting to $host:$port\n" if $opt_d;
socket(SERVER, PF_INET, SOCK_STREAM, getprotobyname("tcp")) or die "epcli: $!";
my $host_addr = sockaddr_in($port, inet_aton($host));
connect(SERVER, $host_addr) or log_ko("$!") and exit 1;

# Identifica la sessione 
my $exit_value = 0;
my $banner     = <SERVER>;
print STDERR "+ > $banner" if $opt_d;

# BITE
print STDERR "* Executing fast bite\n" unless $opt_q;
sendtoA("bit #1 1") or log_ko("Can't bit 1") and exit 1;
waitfor("\\*OK", 10) or log_ko("Can't bit 1") and exit 1;
msleep(500);
sendtoA("bit #2 3") or log_ko("Can't bit 3") and exit 1;
waitfor("\\*OK", 10) or log_ko("Can't bit 3") and exit 1;
msleep(500);
sendtoA("bit #3 8") or log_ko("Can't bit 8") and exit 1;
waitfor("\\*OK", 10) or log_ko("Can't bit 8") and exit 1;

waitfor("!CS blank ready", 4) or log_ko("Can't bite") and next;

log_out("Fast bite passed");

# Poi entra nel ciclo infinito dei test
my $pass;
for ($pass = 1; $pass <= $n_of_pass; $pass++) {
	$Alarm = "";

	sleep 1;

#	# Abortisci
#	print STDERR "* Aborting\n" unless $opt_q;
#	sendto("abort") or log_ko("Problems with abort") and next;
#	waitfor("\\+OK", 6);
#	waitfor("!CS disch ready", 6);

	log_out("<--- Starting pass n. $pass --->");

	# Attivazione RFID
	print STDERR "* Checking rfid\n" unless $opt_q;
#	sendtoA("rfidl #1") or log_ko("Problems with rfid") and next;
	sendtoA("rfidl #1 $PATIENT_ID") or log_ko("Problems with rfidl") and next;
	waitfor("!CS ready rfid", 2) or log_ko("Problems with rfidl") and next;
	waitfor("\\*rfidl", 20) or log_ko("Problems with rfidl") and next;

	($a, $b, $c, $tagType, $tagUID) = split / /, $Found;
	log_out("found tag type $tagType with UID $tagUID");

	waitfor("!CS rfid ready", 20) or log_ko("Problems with rfid") and next;

	sendtoA("rfid #1 $tagUID $PATIENT_ID") or log_ko("Problems with rfid") and next;
	waitfor("!CS ready rfid", 4) or log_ko("Problems with rfid") and next;
	waitfor("!CS rfid ready", 20) or log_ko("Problems with rfid") and next;

	log_out("Rfid passed");
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
	my $timeout = 3;

	sendto($command);

	eval {
		local $SIG{ALRM} = sub { die "alarm clock restart\n" };

		alarm $timeout;
#		$answer = <SERVER>;
#		chomp $answer;
#		print "ricevuto $answer\n" if $opt_d;
#		if ($answer =~ /^\+OK/) {
#			print STDOUT "= $answer\n" unless $opt_q;
#		}
#		else {
#			print STDOUT "  $answer\n" unless $opt_q;
#			$answer = "";
#		}
#		alarm 0;
		while (<SERVER>) {
			chomp;
			print "ricevuto $_\n" if $opt_d;
			if ($_ =~ "!W[23]") {
				$Alarm = $_;
				$answer = $_;
				alarm 0;
				last;
			}
			elsif ($_ =~ /^\+OK/) {
				print STDOUT "= $_\n" unless $opt_q;
				$answer = $_;
				alarm 0;
				last;
			}
			elsif ($_ =~ /^\!FM/) {
				print STDOUT "= $_\n" unless $opt_q;
				$answer = "";
				alarm $timeout;
			}
			else {
				print STDOUT "  $_\n" unless $opt_q;
				$answer = "";
				alarm 0;
				last;
			}
		}
	};
	if ($@ and $@ !~ /alarm clock restart/) {
		$answer = "";
		print STDOUT "- TIMEOUT\n" unless $opt_q;
	}

	$answer =~ s/[\n\r]+//;
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

	$Found = "";
	eval {
		local $SIG{ALRM} = sub { die "alarm clock restart\n" };

		alarm $timeout;
		while (<SERVER>) {
			chomp;
			print "ricevuto $_\n" if $opt_d;
			print "atteso $pattern\n" if $opt_d;
			if ($_ =~ "!W[23]") {
				$Alarm = $_;
				$retval = 1;
				alarm 0;
				last;
			}
			elsif ($_ =~ $pattern) {
#			if ($_ =~ $pattern) {
				print STDOUT "= $_\n" unless $opt_q;
				alarm 0;
				$retval = 1;
				$Found = $_;
				$Found =~ s/[\n\r]+$//;
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

#
# Stampa un log
#
sub log_out($) 
{
	my $string = shift;
	my @x = localtime(time);
	my $now_string = sprintf "%d%02d%02d%02d%02d%02d",
				$x[5]+1900, $x[4]+1, $x[3], $x[2], $x[1], $x[0];

	print STDERR "$now_string $string\n";
	print LOG "$now_string $string\n";
}

#
# Stampa un log di fallimento 
#
sub log_ko($) 
{
	my $string = shift;
	my @x = localtime(time);
	my $now_string = sprintf "%d%02d%02d%02d%02d%02d",
				$x[5]+1900, $x[4]+1, $x[3], $x[2], $x[1], $x[0];

	print STDERR "! $string\n";
	print LOG "$now_string $string\n";
	
	print STDERR "! NOT passed\n";
	print LOG "$now_string NOT passed\n";

	return 1;
}

#
# Attendi per il numero di millisecondi dati (minimo 100)
#
sub msleep($) 
{
	my $ms = shift;
	$ms = 100 if $ms < 100;

	select(undef, undef, undef, $ms/1000);
}

