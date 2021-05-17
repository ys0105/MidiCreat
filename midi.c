/* Autore: Trachi Yassin 08/Maggio/2021 */
/* Finito il: 10/Maggio/2021 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>


typedef unsigned char byte_t;
#ifdef DEBUG
# define D(format, args...) fprintf (stderr, format, ## args)
#else
# define D(format, args...) do { } while (0)
#endif


/* Le note nella prima ottava */
enum NOTE
{
  DO = 0, DOd , RE, REd, MI, FA, FAd,
  SOL   , SOLd, LA, LAd, SI
};

/* Alza la frequenza di una nota. Prossima ottava. */
#define O(nota, ottava) ((12*ottava + nota) % 127)
#define R(base, nota, offset) ((12*(base + offset) + nota) % 127)

/* Ordine di byte inverso */
/* Come vengono presentati i byte in un linguaggio tipo C? */
#undef RBO
#define RBO

#ifdef RBO
# define RB_DWORD(v) ((v&255U)<<24|(v&65280U)<<8|(v&16711680U)>>8|(v&4278190080U)>>24)
# define RB_WORD(v)  ((v&255)<<8|(v&65280)>>8)
#else
# define RB_DWORD(v) (v)
# define RB_WORD(v)  (v)
#endif

struct midifile
{
  int pos;
  int len;
  char buff[10000];
};

struct miditrack
{
  int idx; /* Indice della traccia */
  byte_t events[17000]; /* Note per buffer */
  /* Troppo pigro per usare le liste */
  int len; /* Lunghezza della traccia */
  struct miditrack *next; /* Prossima traccia */
};

struct miditrack_ls
{
  struct miditrack *first;
  struct miditrack *last;
  int tracks; /* Numero di tracce all'interno del file */
};
#define INIT_MIDIBUFF { NULL, NULL, 0 }

#define PUT(name, type, mac, byte)               \
  int put_##name (struct miditrack *m, type t) { \
      type y = mac (t);                          \
      memcpy (m->events + m->len, &y, byte);     \
      m->len+=byte;                              \
      return byte;                               \
      }

PUT (dword, int32_t, RB_DWORD, 4)
PUT (word,  int16_t, RB_WORD , 2)


void
put_byte (struct miditrack *midi, unsigned char byte)
{
  midi->events[midi->len++] = byte;
}

#undef PUT

struct miditrack
*add_miditrack (struct miditrack_ls *ls)
{
  struct miditrack *track = malloc (sizeof (*track));
  if (NULL == track)
    return NULL;
  track->len = 0;
  track->next = NULL;
  if (ls->tracks == 0)
    ls->first = ls->last = track;
  else
    {
      ls->last->next = track;
      ls->last = track;
    }
  track->idx = ++ls->tracks;
  return track;
}


enum { CLEAR_MIDITRACKS = 1 };
/* Unisce una traccia midi al file complessivo */
void
merge (struct miditrack_ls *t, struct midifile *f, int operation)
{
  char *string = NULL; /* Salta tutta la parte di intestazione  */
  struct miditrack *track = NULL;
  struct miditrack *prev = NULL;
  for (track = t->first; track != NULL; )
    {
      string = f->buff + f->pos;
      string[0] = 'M', string[1] = 'T', string[2] = 'r', string[3] = 'k';

      f->pos += 4; /* Avanti 4 */
      track->len += 4;
      track->len = RB_DWORD (track->len);
      memcpy (string + 4, &track->len, 4);
      track->len = RB_DWORD (track->len);
      track->len -= 4;

      f->pos += 4; /* Avanti 4 */
      string += 8;
      memcpy (string, &track->events[0], track->len);

      string += track->len;

      /* Fine della traccia */

      *(string +0) = 0x00;
      *(string +1) = 0xFF;
      *(string +2) = 0x2f;
      *(string +3) = 0x00;
      f->pos += track->len + 4;

      D ("Traccia No. %d inserita (%d + 4 byte scritti)\n", track->idx, track->len);

      prev = track;
      track = track->next;
      if (!(operation & CLEAR_MIDITRACKS))
	continue;
      free (prev);
    }
}

/* Rappresentazione interna del file vero e proprio */
static struct midifile m;
/* Assume come massima dimensione per un campo variabile un long long */
void
varlen_write (struct miditrack *m, long long value)
{
  long long buffer;
  buffer = value & 0x7f;
  while((value >>= 7) > 0)
    {
      buffer <<= 8;
      buffer |= 0x80;
      buffer += (value&0x7F);
    }
  int c = 0;
  while (1)
    {
      c = buffer&0xFF;
      put_byte (m, (unsigned char) c);
      if(buffer & 0x80)
	buffer >>= 8;
      else
	break;
    }
}

void
add_text (struct miditrack *m, const char *string)
{
  int n = strlen (string);
  varlen_write (m, (long long)n);
  int i = 0;
  for (; i < n; ++i)
    m->events[m->len++] = string[i];
}

void
do_header (struct midifile *m, int16_t format_type, int16_t track, int16_t timediv)
{
  m->buff[0] = 0x4D, m->buff[1] = 0x54, m->buff[2] = 0x68, m->buff[3] = 0x64;
  m->buff[4] = 0x00; m->buff[5] = 0x00; m->buff[6] = 0x00; m->buff[7] = 0x06;

  format_type = RB_WORD (format_type);
  track = RB_WORD (track);
  timediv = RB_WORD (timediv); /* Aggiusta */

  memcpy (&m->buff[0] +  8, &format_type, 2);
  memcpy (&m->buff[0] + 10, &track, 2);
  memcpy (&m->buff[0] + 12, &timediv, 2);

  m->pos += 14; /* Header fisso */
}

/* Eventi MIDI */
enum events
{
  NOTE_OFF    = 0x80, NOTE_ON  = 0x90, NOTE_AFTERTOUCH     = 0xA0,
  CONTROLLER  = 0xB0, PCHANGE  = 0xC0, CHANNEL_AFTERTOUCH  = 0xD0,
  PITCH_BEND  = 0xE0
};

#define EVT(name, evt)                                                \
  void evt_##name (struct miditrack *m, long long dt, byte_t channel, \
		   byte_t par1, byte_t par2) {                        \
    varlen_write (m, dt);                                             \
    put_byte (m, evt|channel);                                        \
    put_byte (m, par1);                                               \
    put_byte (m, par2); }

/* Per semplicita implemento solo queste due funzionalita */

EVT(note_off, NOTE_OFF)
EVT(note_on, NOTE_ON)
EVT(note_aftertouch, NOTE_AFTERTOUCH)

#undef EVT

void
do_patch (struct miditrack *m, int ch, int patch)
{
  put_byte (m, 0x00);/* Dt */
  put_byte (m, (unsigned char)0xC0|ch);
  put_byte (m, (unsigned char)patch&0xFF);
}

#define TEMPO(bpm) (60000000UL/(bpm))

/* Per il mio scopo bastano solo questi eventi */
enum meta_events
{
  TEXT_EVENT = 1,
  COPYRIGHT_NOTICE = 2,
  SEQUENCE_NAME = 3,
  LYRIC = 5,
  TEMPO = 0x51,
  TIME_SIG = 0x58
};

long long
time_signature (byte_t num, byte_t denom, long cpn, byte_t bho)
{
  int lg2 = 0;

  /* Calcola il logaritmo in base due */
  while (denom >>= 1) ++lg2;

  long long ret = ((long long)num) << 24;
  ret |= (lg2&0xff)<<16;
  ret |= ((long long) cpn) << 8;
  ret |= bho;
  return ret;
}

void
do_meta_event (struct miditrack *m, long long dt, int type, long long data)
{
  const char *txt = NULL;
  varlen_write (m, dt);
  put_byte (m, 0xFF);
  put_byte (m, (unsigned char) type);
  switch (type)
    {
      /* Trucco: FIXME: Testa prima il puntatore? */
      /* Eventi-testo */
    case TEXT_EVENT:
    case COPYRIGHT_NOTICE:
    case SEQUENCE_NAME:
    case LYRIC:
      txt = (const char *)data;
      if (NULL != txt)
	add_text (m, txt);
      else
	D ("Evento richiede testo");
      break;
    case TIME_SIG: /* Time signature */
      put_byte (m, 0x4);
      put_dword (m, data&4294967295ULL);
      break;
    case TEMPO: /* Imposta il tempo metronomico */
      put_byte (m, 0x3);
      put_byte (m, (data&16711680) >> 16);
      put_byte (m, (data&65280) >> 8);
      put_byte (m, (data&255));
      break;
    default:
      D ("Meta-Evento non riconosciuto %d\n", type);
    }
}

/* Restituisce i byte scritti (-1 se e` capitato un errore) */
int
midi_out (int fd, const struct midifile *m)
{
  int scritti = 0;
  while (-1 != (scritti = write (fd, m->buff, m->pos)))
    {
      /* Tutti i byte sono stati scritti */
      if (scritti == m->pos)
	break;
    }
  return scritti;
}

/* Definisce alcuni di strumenti */
#define FLAUTO 64
#define OBOE 69
#define CLARINETTO 72
#define QUADRA 81
#define DENTE_DI_SEGA 82
#define XILOPHONO 14
#define CELESTA 9
#define OCARINA 79
#define TREMOLO 45
#define CELLO 43
#define VOCE_1 53
#define VOCE_2 54
#define JAZZ 27
#define CRYSTAL 99
#define GOBLIN 102
#define TELEFONO 124
#define SPARO 127
#define SPIAGGIA 122
#define APPLAUSO 126



enum figura
{
  semibreve = 0,
  minima,
  semiminima,
  croma,
  semicroma,
  biscroma,
  semibiscroma
};

/* Puo essere scritta come una macro */
long long
pausa (int ppqn, int figura)
{
  return ppqn >> figura;
}
struct composizione
{
  float numero;
  int nota;
  int evento;
};
static inline void gotoyx(int y,int x)
{
  printf("%c[%d;%df",0x1B,y,x);
}

void toggle_cursor (int show) {
  if (show) 
    fputs("\x1b[?25h", stdout);
  else
    fputs("\x1b[?25l", stdout);
}

int
scala (int nota, int ottava)
{
  enum { MAX = 127 }; /* Massimo rappresnetabile */
  if (ottava == 0)
    return nota;

  /* Da quella ottava stiamo partendo? */
  int ottava_partenza = nota / 12;
  nota %= 12; /* Determina la nota */
  /* Prova a scalare la nota */
  int nota_scalata = nota + ((ottava + ottava_partenza) * 12);
  /* Nota troppo grave */
  if (nota_scalata < 0)
    return nota;
  else if (nota_scalata > MAX)
    {
      if (nota > LA)
	return nota + 0x6C;
      else
	return nota + 0x78;
    }
  return nota_scalata;
}
void
put_note (int nota)
{
  enum { STR_SIZ = 20 };
  char str [20] = {0};
  int o = nota / 12;
  int n = nota % 12;
  switch (n)
    {
      /* Mannaggia all'estensione GNU del C... */
#define DO_CASE(value, fmt, args...)                       \
    case value:						   \
      snprintf (&str[0], STR_SIZ - 1, fmt, ## args);       \
      break
      /* Tutti i casi */
      DO_CASE (DO, "DO(ottava:%d)", o);
      DO_CASE (RE, "RE(ottava:%d)", o);
      DO_CASE (MI, "MI(ottava:%d)", o);
      DO_CASE (FA, "FA(ottava:%d)", o);
      DO_CASE (LA, "LA(ottava:%d)", o);
      DO_CASE (SI, "SI(ottava:%d)", o);
      DO_CASE (DOd, "DO#(ottava:%d)", o);
      DO_CASE (REd, "RE#(ottava:%d)", o);
      DO_CASE (FAd, "FA#(ottava:%d)", o);
      DO_CASE (LAd, "LA#(ottava:%d)", o);
      DO_CASE (SOL, "SOL(ottava:%d)", o);
      DO_CASE (SOLd, "SOL#(ottava:%d)", o);

#undef DO_CASE
    }
  printf ("%s", str);
}
int
main (int argc, char * const argv[])
{
  enum STATO_NOTA { NOTE_OFF = 0, NOTE_ON };

  struct strumento
    {
      const char *nome;
      int valore;
    };
  struct strumento s[] =
  {
      {"PIANO", 0},
      {"XILOPHONO", XILOPHONO},
      {"ONDA_QUADRA", QUADRA},
      {"DENTE_DI_SEGA", DENTE_DI_SEGA},
      {"CELESTA", CELESTA},
      {"OCARINA", OCARINA},
      {"TREMOLO", TREMOLO},
      {"CELLO", CELLO},
      {"VOCE1", VOCE_1},
      {"VOCE2", VOCE_2},
      {"JAZZ", JAZZ},
      {"FX-CRYSTAL", CRYSTAL},
      {"FX-GOBLIN", GOBLIN},
      {"FX-TELEFONO", TELEFONO},
      {"FX-SPARO", SPARO},
      {"FX-SPIAGGIA", SPIAGGIA},
      {"FX-APPLAUSO", APPLAUSO},
      {NULL, 0}
  };
  m.pos = 0;
  /* Risoluzione std di 384 tick per quarto di nota */
  const int risoluzione = 384;
  char filename[] = "prova.mid";
  static struct option long_options[] =
    {
	{"BPM",     required_argument, 0, 'b'},
	{"PPQN",    required_argument, 0, 'q'},
	{"emula",   required_argument, 0, 'e'},
	{"ottava",  required_argument, 0, 'o'},
	{0,         0,                 0,  0 }
    };
  int optidx = 0;
  int opt = -1;
  int bpm = -1;
  int strumento = -1;
  int ottava = 0;
  int chiave = 0;

  int nota_minima = 251, nota_massima = -1;
  assert (sizeof (void *) <= sizeof (long long));
  assert (sizeof (char) == 1);


  while (-1 != (opt = getopt_long (argc, argv, "b:e:o:", long_options, &optidx)))
    {
      switch (opt)
	{
	case 'o':
	  ottava = atoi (optarg);
	  break;
	case 'b': /* Imposta il tempo metronomico per la traccia */
	  bpm = atoi (optarg);
	  if (bpm <= 5)
	    bpm = -1;
	  break;
	case 'e': /* Emula una strumento */
	  for (chiave = 0; s[chiave].nome != NULL; ++chiave)
	    {
	      if (strcmp (s[chiave].nome, optarg) == 0)
		{
		  strumento = s[chiave].valore;
		  /* L'utente ha selezionato uno strumento tra quelli
		   * disponobili */
		  D ("Strumento selezionato: %d\n", strumento);
		  break;
		}
	    }
	  if (strumento == -1)
	    {
	      D ("Strumento non presente nel db locale degli strumenti\n");
	      D ("Assumendo piano forte\n");
	      strumento = 1;
	    }
	  break;
	}
    }
  struct miditrack_ls tracks = INIT_MIDIBUFF;
  struct miditrack *current_track;
  const char *copy_r = "Beethoven (Trachi)";
  int fd = open (filename, O_WRONLY|O_NONBLOCK|O_CREAT|O_TRUNC, 0644);
  if (-1 == fd)
    perror ("open");
  struct composizione note [] =
    {
#include "beethoven.data"
    };
  const char *nome_traccia = "Unica e sola traccia";
  do_header (&m, 1, 2, risoluzione);
  D ("Dimesione header: %d\n", m.pos);
  current_track = add_miditrack (&tracks);
  do_meta_event (current_track, 0, TIME_SIG, time_signature (3, 8, 0x180, 32));
  if (bpm == -1)
    {
      D ("Tempo metronomico non impostato. Assumendo 71 bpm\n");
      bpm = 71;
    }
  do_meta_event (current_track, 0, TEMPO, TEMPO(bpm));

  current_track = add_miditrack (&tracks);
  do_meta_event (current_track, 0, COPYRIGHT_NOTICE, (long long) copy_r);
  do_meta_event (current_track, 0, SEQUENCE_NAME, (long long) nome_traccia);
  if (strumento == -1)
    D ("Nessuno strumento selezionato. Assumendo piano forte\n");
  do_patch (current_track, 0, strumento);
  int i = 0;
  int t;
  while (note[i].nota != -1)
    {
      t = scala (note[i].nota, ottava);
      if (note[i].evento == NOTE_ON)
	{
	  if (nota_minima > t)
	    nota_minima = t;
	  if (nota_massima < t)
	    nota_massima = t;
	  if (i != 0)
	    evt_note_on (current_track,
			 (risoluzione/4) * (note[i].numero - note[i-1].numero),
			 0x0, t , 71);
	  else
	    evt_note_on (current_track, 0x0, 0x0, t, 71);
	}
      else
	evt_note_off (current_track, (risoluzione/4) *
		      (note[i].numero - note[i-1].numero) , 0x0, t, 11);
      ++i;
    }
  merge (&tracks, &m, CLEAR_MIDITRACKS);
#ifdef DEBUG
  D ("Nota piu acuta del brano: ");
  put_note (nota_massima);
  printf ("\n");
  D ("Nota piu grave del brano: ");
  put_note (nota_minima);
  printf ("\n");
  D ("Escursione: %d ottave\n\n", (int) ((float)1/12 * (nota_massima - nota_minima)));
#endif
  D ("\n%d byte scritti su `%s'\n", m.pos, filename);
  if (-1 == midi_out (fd, &m))
    perror ("file_output");
  close (fd);
  return 0;
}
