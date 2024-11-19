#!/usr/bin/perl
#
#	burn-in -- Perl command for burn-in test
#
#	Copyright 2011 by GGH srl for Igea spa
#

use Getopt::Std;
use Socket;
use Socket qw(IPPROTO_TCP TCP_NODELAY);

################################################################################

#	Inizializzazione costanti
my $HOST_NAME = "localhost";		# Host di NFCS
my $EPCORE_PORT = 6969;				# Porta del server	
my $HVC = 1000;
my $HV = 960;						# Carica HV
my $LV = 0;							# Carica LV (ma vedi opt_l)
my $BINDIR = "/opt/eps02/bin";		# Directory dei binari
my $CONFDIR = "/opt/eps02/conf";	# Directory dei file di configurazione
my $USBPORT = "usb2";				# USB port device

################################################################################

#	Prototipi delle subroutine
sub sendto($);
sub sendtoA($);
sub waitfor($$);
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
getopts("dqla:p:h");
my $host       = $opt_a ? $opt_a : $HOST_NAME;
my $port       = $opt_p ? $opt_p : $EPCORE_PORT;

$LV = 0	unless ($opt_l);
$BINDIR = $ENV{BINDIR} if $ENV{BINDIR};			# Se definite da shell, usa
$CONFDIR = $ENV{CONFDIR} if $ENV{CONFDIR};		# quelle (debug only)

# Controlla che gli argomenti dello script siano corretti
if ($opt_h) {
	print STDERR "\nusage: test-epcore.pl [-dqh] [-a host] [-p port]\n";
	print STDERR "       -h print this help\n";
	print STDERR "       -d enable debug output\n";
	print STDERR "       -q quiet mode: dont't print commands/results\n";
	print STDERR "       -l attiva la parte LV (default: solo HV)\n";
	print STDERR "       -a specify server address\t[$HOST_NAME]\n";
	print STDERR "       -p specify server port\n";
	print STDERR "\n";
	exit 1;
}

open(LOG, '>>./test-epcore.log');
close(LOG);
# Bannerino di apertura
log_out("Starting a new burn-in sequence test =========================== ");

# Richiede una connessione con il server
print STDERR "+ Connecting to $host:$port\n" if $opt_d;
socket(SERVER, PF_INET, SOCK_STREAM, getprotobyname("tcp")) or die "epcli: $!";
my $host_addr = sockaddr_in($port, inet_aton($host));
connect(SERVER, $host_addr) or log_ko("$!") and exit 1;

# Identifica la sessione 
my $exit_value = 0;
my $banner     = <SERVER>;
print STDERR "+ > $banner" if $opt_d;

# Abortisci
print STDERR "* Aborting\n";
sendtoA("abort") or log_ko("Problems with abort") and exit 1;
sendtoA("abort") or log_ko("Problems with abort") and exit 1;
waitfor("!CS disch ready", 4);

# Carica
print STDERR "* Charging\n";
sendtoA("charge $HVC $LV") or log_ko("Problems with charge $HVC $LV") and exit 1;
waitfor("!CS chrgn ready", 4) or log_ko("Problems with charge $HV $LV") and exit 1;
sleep 1;
	
# Poi entra nel ciclo infinito dei test
my $pass;
for ($pass = 1;;$pass++) {
	$Alarm = "";

	print STDERR "* Starting pass n. $pass\n" unless $opt_q;
	log_out("<--- Starting pass n. $pass --->");

	#$SIG{INT} = sub { sendto("abort"); 
	#				  sleep 1; 
	#				  sendto("charge 0 0"); 
	#				  waitfor("!CS disch ready");
	#				  die };

	# Trattamento con *******************************************
	
	# Arm
	print STDERR "\t+ Arming\n";
	sendtoA("arm") or log_ko("Problems with arm") and last;
	waitfor("!CS ready armed", 2) or log_ko("Problems with arm") and last;
	sleep 1;

	#Lineare a placchette
	my $N_LV = ($opt_l)? 2: 0;
	my $L_LV = ($opt_l)? 50: 0;

	my $np=8;

	print STDERR "\t+ Sending pars (Placchette)\n";
	sendto("pars Linear $HV 100 1 $LV 10 \\\\") or log_ko("Problems with pars") and last;
	sendtoA("+ 8 100 $N_LV $L_LV 1 3") or log_ko("Problems with pars") and last;
	waitfor("!CS armed wtreat", 2) or log_ko("Problems with pars") and last;
	sleep 0.5;
	
	# Pulse
	print STDERR "\t+ Starting treatment\n";
	sendto("pulse") or log_ko("Problems with pulse") and last;
	waitfor("!W1  503 End of treatment", 10) or log_ko("Problems with pulse") and last;
	my ($a, $n) = split(/=/, $Found);
	$n = ($n+0);
	if ($n == 8) {
         log_out("At pass $pass, $n pulses completed");
	}
	else {
	     log_out("WARNING: at pass $pass, $n pulses completed (8 expected)");
	}
	
	waitfor("!CS trtmt done", 4) or log_ko("Problems with pulse") and last;
	sleep 0.5;

	# Get
	print STDERR "\t+ Retriving data\n";
	sendtoA("get #2") or log_ko("Problems with get") and last;
	waitfor("\\*get #2 Q", 4) or log_ko("Problems with get") and last;

	# Calcola la QoT
#	my @ans = split / /, $Found;
#	log_ko("bad QoT: $ans[4]") if ($ans[3] != "0") and last;

	print STDERR "\t+ Waiting proper state\n";
	waitfor("\\*OK", 20) or log_ko("Problems with get") and last;
	
	sendto("d2r") or log_ko("Problems with d2r") and last;
	
	waitfor("!CS chrgn ready|!CS done ready", 10) or log_ko("Can't reach ready state") and last;
	if ($Alarm) {
		print STDERR "\t--- Got \"$Alarm\"\n";
		$Alarm="";
		waitfor("!CS chrgn ready", 5) or log_ko("Cant return in ready state") and last;

		# Ricarica
#		print STDERR "\t+ Charging\n";
#		sendtoA("charge $HVC $LV") or log_ko("Problems with charge $HVC $LV") and exit 1;
		waitfor("!CS chrgn ready", 4);
	}
	sleep 0.5;
	
	# Attendi 1 secondi e poi riprendi
	print STDERR "* Pass n. $pass done\n";
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
		local $SIG{ALRM} = sub { die "alarm clock restart" };

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
#			if ($_ =~ /^\+OK/) {
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
		local $SIG{ALRM} = sub { die "alarm clock restart" };

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

	open(LOG, '>>./test-epcore.log');
	print LOG "$now_string $string\n";
	close(LOG);
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

	open(LOG, '>>./test-epcore.log');
	print STDERR "! $string\n";
	print LOG "$now_string $string\n";
	
	print STDERR "! NOT passed\n";
	print LOG "$now_string NOT passed\n";
	close(LOG);

	return 1;
}

