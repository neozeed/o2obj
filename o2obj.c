/* Copyright Michael J Johnson 1992 */
/* All Rights Reserved */

/* Modified by Colin Jensen */

//#define INCL_DOSFILEMGR
//#include <os2.h>
/* NOINC */
#include <stdint.h>
#define CHAR    int8_t            /* ch  */
#define SHORT   int16_t           /* s   */
#define LONG    int32_t            /* l   */
typedef uint8_t  UCHAR;   /* uch */
typedef uint16_t USHORT;  /* us  */
typedef uint32_t ULONG;   /* ul  */
typedef uint8_t *PSZ;


//#ifdef __GNUC__
//ULONG _System DosOpen();
//ULONG _System DosQueryFileInfo();
//ULONG _System DosRead();
//ULONG _System DosClose();
//ULONG _System DosWrite();
//#endif
void DosClose(int hFileO);
ULONG DosWrite(int hFileO,char *chRecordType,ULONG size,ULONG ulBytesWritten );

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "a_out.h"
#include "ostab.h"

//#define DEBUG 1
#ifndef O_BINARY
#define O_BINARY 0
#endif
/**** Gnu Object Structures **************************************/

#define UNDEFINED   0
#define ABSOLUTE    2
#define TEXT        4
#define DATA        6
#define BSS         8
#define STABS22    22
#define EXTERIOR    1

typedef struct _OHEADER
       {LONG        lMagicNumber;
        LONG        lTextLength;
        LONG        lDataLength;
        LONG        lUninitDataLength;
        LONG        lSymTableDataLength;
        LONG        lStartAddress;
        LONG        lTextReloLength;
        LONG        lDataReloLength;
       } OHEADER;

typedef OHEADER*    POHEADER;

struct OReloInfo
       {int          Address;
        unsigned     SymNumber:24,
                     PCRelative:1,
                     Length:2,
                     External:1,
                     BSR:1,
                     Disp:1,
                     Nothing:2;
       };

struct SymInfo
       {ULONG       ulIndex;
        CHAR        chType;
        CHAR        chOther;
        SHORT       sDesc;
        LONG        lValue;
       };

   CHAR *               pchOFileBuf;
   ULONG               ulBSSStartSymbol;
   CHAR *               pchTextArea;
   CHAR *               pchDataArea;
   CHAR *               pchTextReloArea;
   CHAR *               pchDataReloArea;
   CHAR *               pchSymReloArea;
   ULONG *              plSymTableLength;
   LONG                 lSymTableLength;
   POHEADER             pOHeader;
   PSZ                  pszSymbolTable;
   LONG                 lSymbolIndex[10000];
   LONG                 lIndex;
    ULONG *symbol_ordinal_xlate;

/***** Microsoft Object Structures ******************************/

#define THEADR          0x80
#define COMENT          0x88
#define SEGDEF          0x99
#define EXTDEF          0x8C
#define LOCEXD          0xB4
#define LPB386          0xB7
#define PUB386          0x91
#define LED386          0xA1
#define FIX386          0x9D
#define COMDEF          0xB0
#define LOCCOM          0xB8
#define LINNUM		0x95
#define LEXTDEF		0xB5
#define LPUBDEF		0xB7

   CHAR                 chRecordType;
   USHORT               usRecordLength;
   ULONG               usFileWriteReturn;
   ULONG                ulBytesWritten;
//   HFILE                hFileO;
   int	hFileO;	//file handle?!
   ULONG                ulCheckSum;
   USHORT               usMainPresent;
   USHORT               usAcrtusedPresent;
   ULONG                ulStart[10000];
   UCHAR                ucFixupQ[1000];
   USHORT               usFixupByteCount;
   ULONG                ulNumberOfSymbols;
   struct SymInfo   *   Symbol;
   ULONG                OutputFileNameOption = 0;
   ULONG                MainOption = 0;
   ULONG                InputFileName = 0;
   CHAR                 MainOptionName[256];
   ULONG                MainEntryAddress = -1;

/******Forward Procedure Declarations ***************************/

void WriteTranslatorHeaderRecord (PSZ pszTModuleName);

void WriteCommentRecord          (USHORT usAttrib,
                                  USHORT usCommentClass,
                                  PSZ    pszComment);

void WriteListOfNamesRecord (void);

void WriteSegmentDefinitionRecord (USHORT  usSegmentName,
                                   USHORT  usClassName,
                                   ULONG   ulLength);

void WriteGroupDefinitionRecord (void);

void WriteFirstFixupRecord(void);

void WriteExtNamesDefRecord(PSZ    pszSymbol,
                            USHORT usTypeIndex);

void WriteLocalExtDefRecord(PSZ       pszSymbol,
                            USHORT    usTypeIndex);

void WriteLocalPublic386Record(PSZ      pszSymbol,
                               ULONG    ulSegmentOffset,
                               USHORT   usTypeIndex);

void WritePublic386Record(PSZ      pszSymbol,
                          USHORT   usGroupIndex,
                          USHORT   usSegmentIndex,
                          ULONG    ulSegmentOffset,
                          USHORT   usTypeIndex);

void WriteLogicalEnumData386Record(USHORT    usSegment,
                                   CHAR *    pchArea,
                                   ULONG     ulLength,
                                   ULONG     ulSize);

void WriteModuleEndRecord(void);

void FixupTextChunk(CHAR *      pchReloArea,
                    ULONG       lReloLength,
                    ULONG       ulStart,
                    ULONG       ulNext);

void WriteCommunalNamesDefRecord(PSZ     pszSymbol,
                                 USHORT  usTypeIndex,
                                 ULONG   ulSize);

void WriteLocalCommunalNamesDefRecord(PSZ     pszSymbol,
                                      USHORT  usTypeIndex,
                                      ULONG   ulSize);

void WriteStackSegment(void);

void WriteModuleLineNumbers(void);


/****************************************************************/

void bad_usage(void)
{
   printf ("\nUsage: o2obj [-o obj-filename] [-main entry-name] o-filename\n");
   exit (1);
}

/****************************************************************/

int
main (ULONG     argc,
      PSZ       argv[])

  {/*- Program Data Declarations ----------------------------------------*/

   ULONG       i;
   ULONG       j;
   ULONG       ulNumberOfChunks;
   ULONG       ulChunkSize = 0x3C0;

   ULONG                ulActionTaken;
   ULONG                BytesRead;
   ULONG               FileOpenReturn;
   ULONG               FileReadReturn;
   ULONG                x;
   CHAR                 szInputFileName[256];
   CHAR                 szOutputFileName[256];
   PSZ                  pszSymbol;

   ULONG xlate_sym_ordinal;

   /*--- Program Data Structures -----------------------------------------*/

   struct OReloInfo *   ReloInfo;
//   FILESTATUS           OFileStatus;
  struct stat OFileStatus;

   /*---------------------------------------------------------------------*/

   for (i = 1; i < argc; ++i)
     {
        if (argv[i][0] == '-')
        {
           if (strcmp (argv[i], "-o") == 0)
           {
              if (OutputFileNameOption || i + 1 >= argc)
                 bad_usage();

              strcpy (szOutputFileName, argv[i+1]);
              ++i;
              OutputFileNameOption = 1;
           }
           else
              if (strcmp (argv[i], "-main") == 0)
              {
                 if (MainOption || i+1 >= argc)
                    bad_usage();

                 strcpy (MainOptionName, argv[i+1]);
                 ++i;
                 MainOption = 1;
              }
        }
        else
        {
           if (InputFileName)
              bad_usage();

           strcpy (szInputFileName, argv[i]);
           InputFileName = 1;
        }
     }

   if (!OutputFileNameOption)
   {
      strcpy (szOutputFileName, szInputFileName);
      strcat (szOutputFileName, "bj");
   }
#if 0
   FileOpenReturn = DosOpen (szInputFileName,
                             &hFileO,
                             &ulActionTaken,
                             0,
                             FILE_NORMAL,
                             FILE_OPEN,
                             OPEN_ACCESS_READONLY |
                             OPEN_SHARE_DENYNONE,
			     (PEAOP2) NULL);

   if (FileOpenReturn) {printf ("Open error: %d\n", FileOpenReturn);
                        return 1;
                       }

   FileReadReturn = DosQueryFileInfo (hFileO,
                                      0x0001,
                                      &OFileStatus,
                                      sizeof (OFileStatus));

   if (FileReadReturn) {printf ("File Query error: %d\n", FileReadReturn);
                        exit(1);
                       }

   pchOFileBuf = (CHAR *) malloc(OFileStatus.cbFile);

   FileReadReturn = DosRead (hFileO,
                             pchOFileBuf,
                             OFileStatus.cbFile,
                             &BytesRead);

#else
#ifdef DEBUG
printf("reading szInputFileName: %s\n",szInputFileName);
#endif
hFileO=open(szInputFileName,O_BINARY|O_RDONLY);
if (hFileO == -1) {
	printf ("Open error: %d\n", FileOpenReturn);
        return 1;
        }
if (fstat(hFileO, &OFileStatus) == -1) {
	printf ("File Query error: %d\n", FileReadReturn);
        exit(1);
        }
#ifdef DEBUG
printf("OFileStatus.st_size: %d\n",OFileStatus.st_size);
#endif
pchOFileBuf = (CHAR *) malloc(OFileStatus.st_size);
BytesRead=read(hFileO,pchOFileBuf,OFileStatus.st_size);
#endif
#ifdef DEBUG

   printf ("\nO File bytes read %d\n\n", BytesRead);

#endif

   DosClose (hFileO);

   /*----------------------------------------------------------------------*/

   pOHeader = (POHEADER) pchOFileBuf;

   pchTextArea = (CHAR *) (pOHeader + 1);

   pchDataArea = (CHAR *) (pchTextArea + (pOHeader->lTextLength));

   pchTextReloArea = (CHAR *) (pchDataArea + (pOHeader->lDataLength));

   pchDataReloArea = (CHAR *) (pchTextReloArea + (pOHeader->lTextReloLength));

   pchSymReloArea = (CHAR *) (pchDataReloArea + (pOHeader->lDataReloLength));

   plSymTableLength = (ULONG *) (pchSymReloArea + (pOHeader->lSymTableDataLength));

   lSymTableLength = *plSymTableLength;

   pszSymbolTable = (CHAR *) (plSymTableLength + 1);

   /*----------------------------------------------------------------------*/

#ifdef DEBUG

   printf ("O File Header:\n");
   printf ("Magic number .................... 0x%lX\n", pOHeader->lMagicNumber);
   printf ("Text length ..................... 0x%lX\n", pOHeader->lTextLength);
   printf ("Data length ..................... 0x%lX\n", pOHeader->lDataLength);
   printf ("Uninitialized data length ....... 0x%lX\n", pOHeader->lUninitDataLength);
   printf ("Symbol table length ............. 0x%lX\n", pOHeader->lSymTableDataLength);
   printf ("Start address ................... 0x%lX\n", pOHeader->lStartAddress);
   printf ("Length of text relocation info .. 0x%lX\n", pOHeader->lTextReloLength);
   printf ("Length of data relocation info .. 0x%lX\n", pOHeader->lDataReloLength);

#endif

   /*-----------------------------------------------------------------------*/


#ifdef DEBUG

   if (pOHeader->lTextReloLength)
       printf ("\nText Relocation Information\n");

   ReloInfo = (struct OReloInfo *) pchTextReloArea;

   Symbol = (struct SymInfo *) pchSymReloArea;

   for (x = 0;
        x < pOHeader->lTextReloLength/8;
        x++)
     {CHAR pszSymbolType[100];
     if (ReloInfo[x].External == 1)
          {pszSymbol = &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4];
           switch (Symbol[ReloInfo[x].SymNumber].chType)
           {
            case EXTERIOR:
		sprintf(pszSymbolType, "External: %d", 
			ReloInfo[i].SymNumber+1);
/*                strcpy(pszSymbolType, "External");*/
                break;
             case DATA:
                strcpy(pszSymbolType, "Data");
                break;
             case DATA | EXTERIOR:
                strcpy(pszSymbolType, "Data External");
                break;
             case TEXT:
                strcpy(pszSymbolType, "Text");
                break;
             case TEXT | EXTERIOR:
                strcpy(pszSymbolType, "Text External");
                break;
             case BSS:
                strcpy(pszSymbolType, "Bss");
                break;
             default:
                strcpy(pszSymbolType, "UNKNOWN");
                break;
           } /* endswitch */
          }
      else
         {sprintf (pszSymbolType, "%lX", *((LONG *)(pchTextArea + ReloInfo[x].Address)));
          switch (ReloInfo[x].SymNumber)
          {
           case TEXT:
               pszSymbol = ".text";
               break;
           case DATA:
               pszSymbol = ".data";
               break;
           case BSS:
               pszSymbol = ".bss";
               break;
          } /* endswitch */
         }

      printf ("0x%08X %6d %1d %1d %1d %1d %1d %1d %s %s\n", 
	      ReloInfo[x].Address,
	      ReloInfo[x].SymNumber,
	      ReloInfo[x].PCRelative,
	      ReloInfo[x].Length,
	      ReloInfo[x].External,
	      ReloInfo[x].BSR,
	      ReloInfo[x].Disp,
	      ReloInfo[x].Nothing,
	      pszSymbol,
	      pszSymbolType);
     }

   /*----------------------------------------------------------------------*/

   if (pOHeader->lDataReloLength)
       printf ("\nData Relocation Information\n");

   ReloInfo = (struct OReloInfo *) pchDataReloArea;

   for (x = 0;
        x < pOHeader->lDataReloLength/8;
        x++)
     {CHAR pszSymbolType[100];
      if (ReloInfo[x].External == 1)
          {pszSymbol = &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4];
           switch (Symbol[ReloInfo[x].SymNumber].chType)
           {
            case EXTERIOR:
                strcpy(pszSymbolType, "External");
                break;
             case DATA:
                strcpy(pszSymbolType, "Data");
                break;
             case DATA | EXTERIOR:
                strcpy(pszSymbolType, "Data External");
                break;
             case TEXT:
                strcpy(pszSymbolType, "Text");
                break;
             case TEXT | EXTERIOR:
                strcpy(pszSymbolType, "Text External");
                break;
             case BSS:
                strcpy(pszSymbolType, "Bss");
                break;
             default:
                strcpy(pszSymbolType, "UNKNOWN");
                break;
           } /* endswitch */
          }
      else
         {sprintf (pszSymbolType, "%lX", *((LONG *)(pchDataArea + ReloInfo[x].Address)));
          switch (ReloInfo[x].SymNumber)
          {
           case TEXT:
               pszSymbol = ".text";
               break;
           case DATA:
               pszSymbol = ".data";
               break;
           case BSS:
               pszSymbol = ".bss";
               break;
          } /* endswitch */
         }
      printf ("0x%8X %6d %1d %1d %1d %1d %1d %1d %s %s\n", ReloInfo[x].Address,
                                                           ReloInfo[x].SymNumber,
                                                           ReloInfo[x].PCRelative,
                                                           ReloInfo[x].Length,
                                                           ReloInfo[x].External,
                                                           ReloInfo[x].BSR,
                                                           ReloInfo[x].Disp,
                                                           ReloInfo[x].Nothing,
                                                           pszSymbol,
                                                           pszSymbolType);
     }

   /*----------------------------------------------------------------------*/

   if (pOHeader->lSymTableDataLength)
       printf ("\nSymbol Relocation Information\n");

   Symbol = (struct SymInfo *) pchSymReloArea;

   ulNumberOfSymbols = pOHeader->lSymTableDataLength/12;

   /********************************************************************\
    *  Scanning through the Symbol Table in Symbol Info order          *
   \********************************************************************/
   for (x = 0;
        x < ulNumberOfSymbols;
        x++)
     {pszSymbol = &pszSymbolTable[Symbol[x].ulIndex-4];
      if (Symbol[x].chType == 0x44)
          pszSymbol = "Line Number Info";
      printf ("%4d %32s %1X %1X %d %lX\n", x,
                                           pszSymbol,
                                           Symbol[x].chType,
                                           Symbol[x].chOther,
                                           Symbol[x].sDesc,
                                           Symbol[x].lValue);
     }

#endif

   /* Create a table of symbol numbers that reflects the symbol order
    * as they will appear in the OMF file rather than as they appear
    * in the a.out file
    */

   Symbol = (struct SymInfo *) pchSymReloArea;
   ulNumberOfSymbols = pOHeader->lSymTableDataLength/12;
   if (ulNumberOfSymbols) {
       symbol_ordinal_xlate = 
	   (ULONG *) malloc(sizeof(ULONG) * ulNumberOfSymbols);
   }

   xlate_sym_ordinal = 0;
   for (x = 0; x < ulNumberOfSymbols; x++) {
       /* If the symbol will be injected into the OMF stream,
	* assign it an OMF ordinal
	*/
       switch(Symbol[x].chType) {
       case UNDEFINED | EXTERIOR:
       case TEXT:
       case DATA:
       case TEXT | EXTERIOR:
       case DATA | EXTERIOR:
       case BSS:
       case STABS22:
#ifdef DEBUG
	   printf("Mapping a.out ordinal #%d to omf ordinal #%d\n",
		  x, xlate_sym_ordinal);
#endif
	   symbol_ordinal_xlate[x] = xlate_sym_ordinal++;
       }
   }
   
   /******************************************************/
   /*  If this is the main module find the start address */
   /******************************************************/

   if (MainOption)
   {
      if (pOHeader->lSymTableDataLength)
      {
         Symbol = (struct SymInfo *) pchSymReloArea;

         ulNumberOfSymbols = pOHeader->lSymTableDataLength/12;

         /************************************************************\
          *  Scanning through the Symbol Table in Symbol Info order  *
         \************************************************************/
         for (x = 0;
              x < ulNumberOfSymbols;
              x++)
           {
              pszSymbol = &pszSymbolTable[Symbol[x].ulIndex-4];
              if (strcmp (MainOptionName, pszSymbol) == 0)
              {
                 MainEntryAddress = Symbol[x].lValue;
                 break;
              }
           }
      }
      if (MainEntryAddress == -1)
      {
         printf ("\nMain entry symbol not found -- assuming text origin\n");
         MainEntryAddress = 0;
      }
   }

   /*********************************************************************/
   /*  Locate the symbol that starts the BSS.  This will be used to     */
   /*  generate named fixup records from un-named fixup records.        */
   /*********************************************************************/

   /*  See if there is any BSS segment */

   Symbol = (struct SymInfo *) pchSymReloArea;

   ulNumberOfSymbols = pOHeader->lSymTableDataLength/12;

   if (pOHeader->lUninitDataLength)

     { /*  There is a BSS segment */
      /*-----------------------------------------------------------------*/
      LONG    lBSSStart;
      /*-----------------------------------------------------------------*/
      lBSSStart = pOHeader->lTextLength + pOHeader->lDataLength;

      for (x = 0;
	   x < ulNumberOfSymbols;
	   x++)
	{
	 if (Symbol[x].lValue == lBSSStart && Symbol[x].chType == BSS)
	   {
	    /*  Save the symbol number of the symbol starting the BSS */
	    ulBSSStartSymbol = x;

	    /*********************************************************/
	    /* Set the 'value' of the symbol to the size of the BSS  */
	    /* in order to generate a LOCCOM record with the correct */
	    /* size of the BSS                                       */
	    /*********************************************************/
	    Symbol[x].lValue = pOHeader->lUninitDataLength;

#ifdef DEBUG

	    printf ("BSS starts at symbol # %d\n", x);

#endif

	   }
	}
     }

  /*---------------------------------------------------------------------*/

  ReloInfo = (struct OReloInfo *) pchTextReloArea;

  for (x = 0;
       x < pOHeader->lTextReloLength/8;
       x++)
    {if (ReloInfo[x].External)
        {switch (Symbol[ReloInfo[x].SymNumber].chType)
           {
            case EXTERIOR:
/*            case BSS: */
                if (ReloInfo[x].PCRelative == 0 /*&&
                    Symbol[ReloInfo[x].SymNumber].lValue == 0*/)
                  break;
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) = 0L;
                break;

            case TEXT | EXTERIOR:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) = 0L;
                break;

            case DATA:
            case DATA | EXTERIOR:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) -= pOHeader->lTextLength;
                break;
/*
            case BSS:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) -= (pOHeader->lTextLength) +
                                                               (pOHeader->lDataLength);
                break;
*/
            default:
                printf ("\nFound named symbol %s Type %d during address correction for text\n",
                        &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4],
                        Symbol[ReloInfo[x].SymNumber].chType);
           }
        }
    else
        {switch (ReloInfo[x].SymNumber & 0x00000E)
           {
            case EXTERIOR:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) = 0L;
                break;

            case TEXT:
                break;

            case DATA:
            case DATA | EXTERIOR:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) -= pOHeader->lTextLength;
                break;

            case BSS:
                *((LONG*) (pchTextArea + ReloInfo[x].Address)) -= (pOHeader->lTextLength) +
                                                                  (pOHeader->lDataLength);
                break;

            default:
                printf ("\nFound symbol %s Type %d during address correction for text\n",
                        &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4],
                        Symbol[ReloInfo[x].SymNumber].chType);
           }
        }

    }
  /*------------------------------------------------------------------------*/

  ReloInfo = (struct OReloInfo *) pchDataReloArea;

  for (x = 0;
        x < pOHeader->lDataReloLength/8;
        x++)
    {if (ReloInfo[x].External)
        {switch (Symbol[ReloInfo[x].SymNumber].chType)
           {
            case EXTERIOR:
/*            case BSS:*/
#if 0
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) = 0L;
#endif
                break;

            case TEXT | EXTERIOR:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) = 0L;
                break;

            case DATA:
            case DATA | EXTERIOR:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) -= pOHeader->lTextLength;
                break;
/*
            case BSS:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) -= (pOHeader->lTextLength) +
                                                               (pOHeader->lDataLength);
                break;
*/
            default:
                printf ("\nFound named symbol %s Type %d during address correction for data\n",
                        &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4],
                        Symbol[ReloInfo[x].SymNumber].chType);
           }
        }
    else
        {switch (ReloInfo[x].SymNumber & 0x00000E)
           {
            case EXTERIOR:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) = 0L;
                break;

            case TEXT:
                break;

            case DATA:
            case DATA | EXTERIOR:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) -= pOHeader->lTextLength;
                break;

            case BSS:
                *((LONG*) (pchDataArea + ReloInfo[x].Address)) -= (pOHeader->lTextLength) +
                                                                  (pOHeader->lDataLength);
                break;

            default:
                printf ("\nFound symbol %s Type %d during address correction for data\n",
                        &pszSymbolTable[Symbol[ReloInfo[x].SymNumber].ulIndex-4],
                        Symbol[ReloInfo[x].SymNumber].chType);
           }
        }

    }
  /*------------------------------------------------------------------------*/
#if 0
   FileOpenReturn = DosOpen (szOutputFileName,
                             &hFileO,
                             &ulActionTaken,
                             0L,
                             FILE_NORMAL,
                             FILE_CREATE | FILE_TRUNCATE,
                             OPEN_ACCESS_READWRITE |
                             OPEN_SHARE_DENYREADWRITE,
			     (PEAOP2) NULL);

   if (FileOpenReturn) {printf ("Open Obj error: %d\n", FileOpenReturn);
                        return 1;
                       }
#else
#ifdef DEBUG
printf("writing file szOutputFileName: %s\n",szOutputFileName);
#endif
   //FileOpenReturn = open (szOutputFileName,O_BINARY|O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR );
   FileOpenReturn = open (szOutputFileName,O_BINARY|O_RDWR|O_CREAT|O_TRUNC,0666 );
   if (FileOpenReturn== -1) {
	printf ("Open Obj error: %d\n", FileOpenReturn);
        return 1;
        }
#endif
   /*-----------------------------------------------------------------------*/

   WriteTranslatorHeaderRecord (szOutputFileName);

   WriteCommentRecord (0, 0, "\x05GNU C");

   if (MainOption)
      WriteCommentRecord (0, 0x9f, "OS2");
   else
      WriteCommentRecord (0, 0x9f, "LIBCRT");

   WriteCommentRecord (0, 0x9f, "LIBGCC");

   WriteCommentRecord (0, 0x9d, "3sO");

   WriteCommentRecord (0, 0xA1, "\001CV");

   WriteListOfNamesRecord ();

   WriteSegmentDefinitionRecord (3, 4, pOHeader->lTextLength);

   WriteSegmentDefinitionRecord (5, 6, pOHeader->lDataLength);

   WriteSegmentDefinitionRecord (7, 7, 0L);

   WriteSegmentDefinitionRecord (8, 9, pOHeader->lUninitDataLength);

   if (MainOption)
      WriteSegmentDefinitionRecord (10, 10, 4096);

   WriteGroupDefinitionRecord ();

   WriteFirstFixupRecord ();

   /*----------------------------------------------------------------------*/

   lIndex = 1;

   for (x = 0;
        x < ulNumberOfSymbols;
        x++)
     {pszSymbol = &pszSymbolTable[Symbol[x].ulIndex-4];

      switch (Symbol[x].chType)
        {
         case UNDEFINED:
             printf ("Found undefined symbol: %s 0\n", pszSymbol);
             break;

         case UNDEFINED | EXTERIOR:
             lSymbolIndex[x] = lIndex++;
             if (Symbol[x].chType == EXTERIOR && Symbol[x].lValue)
               WriteCommunalNamesDefRecord (pszSymbol, 0, Symbol[x].lValue); /* B0 */
             else
               WriteExtNamesDefRecord (pszSymbol, 0);   /* 8C */
             break;

         case TEXT:
             lSymbolIndex[x] = lIndex++;
             WriteLocalExtDefRecord (pszSymbol, 0);
             WriteLocalPublic386Record (pszSymbol, Symbol[x].lValue, 0);
             break;

         case DATA:
             lSymbolIndex[x] = lIndex++;
             WriteLocalExtDefRecord (pszSymbol, 0);
             WriteLocalPublic386Record (pszSymbol, Symbol[x].lValue, 0);
             break;

         case TEXT | EXTERIOR:
             lSymbolIndex[x] = lIndex++;
             WriteExtNamesDefRecord (pszSymbol, 0);   /* 8C */
             WritePublic386Record (pszSymbol,
                                   0,
                                   1,
                                   Symbol[x].lValue,
                                   0);   /* 91 */
             break;

         case DATA | EXTERIOR:
             lSymbolIndex[x] = lIndex++;
             WriteExtNamesDefRecord (pszSymbol, 0);   /* 8C */
             WritePublic386Record (pszSymbol,
                                   1,
                                   2,
                                   Symbol[x].lValue - pOHeader->lTextLength,
                                   0);   /* 91 */
             break;

         case BSS:
             lSymbolIndex[x] = lIndex++;
             if (x == ulBSSStartSymbol)
               {
                WriteLocalCommunalNamesDefRecord (pszSymbol, 0, Symbol[x].lValue); /* B0 */
               }
             else
               {
                WriteLocalCommunalNamesDefRecord (pszSymbol, 0, 1); /* B0 */
               }
             break;

         case BSS | EXTERIOR:
             printf ("Found External BSS symbol: %s - Type %d\n", pszSymbol, BSS | EXTERIOR);
             break;

         case ABSOLUTE:
             printf ("Found Absolute symbol: %s - Type %d\n", pszSymbol, ABSOLUTE);
             break;

         case ABSOLUTE | EXTERIOR:
             printf ("Found External Absolute symbol: %s - Type %d\n", pszSymbol, ABSOLUTE | EXTERIOR);
             break;

         case STABS22:   /* This is for G++ __DTOR__ and __CTOR__ */
             WriteExtNamesDefRecord (pszSymbol, 0);   /* 8C */
             break;

        }
     }

   /*----------------------------------------------------------------------*/
   for (x = 0;
        x < ulNumberOfSymbols;
        x++)
     {pszSymbol = &pszSymbolTable[Symbol[x].ulIndex-4];

      switch (Symbol[x].chType)
        {
         case TEXT | EXTERIOR:
             break;

         case DATA | EXTERIOR:
             break;

        }
     }

   /*----------------------------------------------------------------------*/

   usMainPresent = 0;
   usAcrtusedPresent = 0;

   for (x = 0;
        x < ulNumberOfSymbols;
        x++)
     {pszSymbol = &pszSymbolTable[Symbol[x].ulIndex-4];

      if (strcmp(pszSymbol, "_main") == 0)
        usMainPresent = 1;

      if (strcmp(pszSymbol, "__acrtused") == 0)
        usAcrtusedPresent = 1;
     }

   if (usMainPresent && !usAcrtusedPresent && !MainOption)
     WriteExtNamesDefRecord ("__acrtused", 0);   /* 8C */


   /*----------------------------------------------------------------------*/

/*   WriteCommentRecord (0, 0xA2, "\1");*/
   WriteCommentRecord (0, 0xA2, 0);

  /*----------------------------------------------------------------------*/

   ulNumberOfChunks = pOHeader->lTextLength/ulChunkSize;
   if (pOHeader->lTextLength % ulChunkSize)
   ulNumberOfChunks++;

   ulStart[0] = 0;
   for (i=1; i < ulNumberOfChunks+1; i++)
      ulStart[i] = i * ulChunkSize;

   ulStart[ulNumberOfChunks] = pOHeader->lTextLength;

   ReloInfo = (struct OReloInfo *) pchTextReloArea;

   for (j=0;
        j < ulNumberOfChunks;
        j++)
      {
       for (i=0;
            i < pOHeader->lTextReloLength/8;
            i++)
          {if (ReloInfo[i].Address > ulStart[j+1] -4 &&
               ReloInfo[i].Address < ulStart[j+1])
             ulStart[j+1] = ReloInfo[i].Address;
          }
      }

   for (j = 0;
        j < ulNumberOfChunks;
        j++)
      {WriteLogicalEnumData386Record (1, pchTextArea, ulStart[j], ulStart[j+1] - ulStart[j]);
       FixupTextChunk(pchTextReloArea,
                      pOHeader->lTextReloLength,
                      ulStart[j],
                      ulStart[j+1]);
      }

   /*----------------------------------------------------------------------*/

   ulNumberOfChunks = pOHeader->lDataLength/ulChunkSize;
   if (pOHeader->lDataLength % ulChunkSize)
   ulNumberOfChunks++;

   ulStart[0] = 0;
   for (i=1; i < ulNumberOfChunks+1; i++)
      ulStart[i] = i * ulChunkSize;

   ulStart[ulNumberOfChunks] = pOHeader->lDataLength;

   ReloInfo = (struct OReloInfo *) pchDataReloArea;

   for (j=0;
        j < ulNumberOfChunks;
        j++)
      {
       for (i=0;
            i < pOHeader->lDataReloLength/8;
            i++)
          {if (ReloInfo[i].Address > ulStart[j+1] -4 &&
               ReloInfo[i].Address < ulStart[j+1])
             ulStart[j+1] = ReloInfo[i].Address;
          }
      }

   for (j = 0;
        j < ulNumberOfChunks;
        j++)
      {WriteLogicalEnumData386Record (2, pchDataArea, ulStart[j], ulStart[j+1] - ulStart[j]);
       FixupTextChunk(pchDataReloArea,
                        pOHeader->lDataReloLength,
                        ulStart[j],
                        ulStart[j+1]);
      }

   /*----------------------------------------------------------------------*/

   if (MainOption)
      WriteStackSegment();

   /*----------------------------------------------------------------------*/

   /* Write line number info into OMF file */
   WriteModuleLineNumbers();

   /*----------------------------------------------------------------------*/


   WriteModuleEndRecord();

   /*----------------------------------------------------------------------*/

   DosClose (hFileO);

   return (0);
  }

/**************************************************************************/

void WriteTranslatorHeaderRecord (PSZ pszTModuleName)
{
   ULONG        ulModuleNameLength;
   ULONG        x;

   chRecordType = THEADR;
   usRecordLength = (USHORT) (strlen (pszTModuleName) + 2);

   ulModuleNameLength = strlen (pszTModuleName);

   ulCheckSum = THEADR + (ULONG) usRecordLength + ulModuleNameLength;

   for (x = 0;
        x < ulModuleNameLength;
        x++)
     ulCheckSum += pszTModuleName[x];

   ulCheckSum = 256 - (ulCheckSum & 0xFF);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulModuleNameLength,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszTModuleName,
                               (ULONG) strlen (pszTModuleName),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record ulCheckSum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/****************************************************************************/

void WriteCommentRecord (USHORT usAttrib,
                         USHORT usCommentClass,
                         PSZ    pszComment)
{
   ULONG            x;

   chRecordType = COMENT;
   if (pszComment)
      usRecordLength = (USHORT) (strlen (pszComment) + 3);
   else
      usRecordLength = 4;

   ulCheckSum = chRecordType + usRecordLength + usAttrib + usCommentClass;

   if (pszComment)
      for (x = 0;
           x < strlen (pszComment);
           x++)
         ulCheckSum += pszComment[x];
      
   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 88H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 88H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usAttrib,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 88H record attributes: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usCommentClass,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 88H record comment class: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   if (pszComment)
      usFileWriteReturn = DosWrite (hFileO,
                                      pszComment,
                                      (ULONG) strlen (pszComment),
                                      &ulBytesWritten);
   else
      usFileWriteReturn = DosWrite (hFileO,
                                      "\0",
                                      (ULONG) 1,
                                      &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 88H record translator name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 80H record ulCheckSum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/**************************************************************************/

void WriteListOfNamesRecord (void)
{
   if (MainOption)
      usFileWriteReturn = DosWrite (hFileO,
                                  "\226\070\0\006DGROUP",
                                  (ULONG) 10,
                                  &ulBytesWritten);
   else
      usFileWriteReturn = DosWrite (hFileO,
                                  "\226\054\0\0",
                                  (ULONG) 4,
                                  &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 96H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               "\004FLAT\005_TEXT\004CODE\005_DATA",
                               (ULONG) 22,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 96H record part 1: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   if (MainOption)
      usFileWriteReturn = DosWrite (hFileO,
                             "\004DATA\005CONST\004_BSS\003BSS\005STACK\217",
                             (ULONG) 27,
                             &ulBytesWritten);
   else
      usFileWriteReturn = DosWrite (hFileO,
                                  "\004DATA\005CONST\004_BSS\003BSS\355",
                                  (ULONG) 21,
                                  &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 96H record part 2: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/***************************************************************************/

void WriteSegmentDefinitionRecord (USHORT  usSegmentName,
                                   USHORT  usClassName,
                                   ULONG   ulLength)
{
   USHORT usSegmentAttributes;
   USHORT usOverlayName;

   chRecordType = SEGDEF;
   usRecordLength = 9;

   if (usSegmentName == 10)
      usSegmentAttributes = 0xb5;
   else
      usSegmentAttributes = 0xa9;

   usOverlayName = 1;

   ulCheckSum = chRecordType + usRecordLength + usSegmentName + usClassName;
   ulCheckSum += ulLength & 0xff;
   ulCheckSum += (ulLength >> 8) & 0xff;
   ulCheckSum += (ulLength >> 16) & 0xff;
   ulCheckSum += (ulLength >> 24) & 0xff;
   ulCheckSum += usSegmentAttributes + usOverlayName;
   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usSegmentAttributes,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record attributes: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulLength,
                               (ULONG) 4,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usSegmentName,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record segment name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usClassName,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record class name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usOverlayName,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record overlay name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 99H record checksum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/***************************************************************************/

void WriteGroupDefinitionRecord (void)
{
   if (MainOption)
      usFileWriteReturn = DosWrite (hFileO,
                                  "\232\12\0\1\377\2\377\3\377\4\377\5\x51",
                                  (ULONG) 13,
                                  &ulBytesWritten);

   usFileWriteReturn = DosWrite (hFileO,
                               "\232\2\0\2\142",
                               (ULONG) 5,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 9AH record: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/***************************************************************************/

void WriteFirstFixupRecord(void)
{
   usFileWriteReturn = DosWrite (hFileO,
                               "\235\15\0\0\3\1\2\2\1\3\4\100\1\105\1\277",
                               (ULONG) 16,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing First 9DH record: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

}

/**************************************************************************/

void WriteExtNamesDefRecord (PSZ      pszSymbol,
                             USHORT   usTypeIndex)
  {/*--------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   /*--------------------------------------------------------------------*/

   chRecordType = EXTDEF;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 3);

   ulCheckSum = chRecordType + usRecordLength + usTypeIndex + strlen(pszSymbol);

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usTemp = (USHORT) strlen (pszSymbol);

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ch check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteLocalExtDefRecord (PSZ      pszSymbol,
                             USHORT   usTypeIndex)
  {/*--------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   /*--------------------------------------------------------------------*/

   chRecordType = LOCEXD;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 3);

   ulCheckSum = chRecordType + usRecordLength + usTypeIndex + strlen(pszSymbol);

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usTemp = (USHORT) strlen (pszSymbol);

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B4h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteLocalPublic386Record(PSZ      pszSymbol,
                               ULONG    ulSegmentOffset,
                               USHORT   usTypeIndex)
  {/*----------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   /*----------------------------------------------------------------------*/

   chRecordType = LPB386;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 9);

   ulCheckSum = 1 +
                chRecordType +
                usRecordLength +
                usTypeIndex +
                strlen(pszSymbol);

   ulCheckSum += ulSegmentOffset & 0xff;
   ulCheckSum += (ulSegmentOffset >> 8) & 0xff;
   ulCheckSum += (ulSegmentOffset >> 16) & 0xff;
   ulCheckSum += (ulSegmentOffset >> 24) & 0xff;

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               "\0\1",
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usTemp = (USHORT) strlen (pszSymbol);

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulSegmentOffset,
                               (ULONG) 4,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h segment offset: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B7h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WritePublic386Record(PSZ      pszSymbol,
                          USHORT   usGroupIndex,
                          USHORT   usSegmentIndex,
                          ULONG    ulSegmentOffset,
                          USHORT   usTypeIndex)
  {/*----------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   /*----------------------------------------------------------------------*/

   chRecordType = PUB386;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 9);

   ulCheckSum = usGroupIndex +
                usSegmentIndex +
                chRecordType +
                usRecordLength +
                usTypeIndex +
                strlen(pszSymbol);

   ulCheckSum += ulSegmentOffset & 0xff;
   ulCheckSum += (ulSegmentOffset >> 8) & 0xff;
   ulCheckSum += (ulSegmentOffset >> 16) & 0xff;
   ulCheckSum += (ulSegmentOffset >> 24) & 0xff;

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &usGroupIndex,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h group index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &usSegmentIndex,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h segment index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
   usTemp = (USHORT) strlen (pszSymbol);

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulSegmentOffset,
                               (ULONG) 4,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h segment offset: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 91h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteLogicalEnumData386Record(USHORT    usSegment,
                                   CHAR *    pchArea,
                                   ULONG     ulStart,
                                   ULONG     ulSize)
{

   chRecordType = LED386;
   usRecordLength = ulSize + 6;

   ulCheckSum = chRecordType +
                (usRecordLength & 0xFF) +
                ((usRecordLength >> 8) & 0xFF) +
                usSegment;

   ulCheckSum += ulStart & 0xff;
   ulCheckSum += (ulStart >> 8) & 0xff;
   ulCheckSum += (ulStart >> 16) & 0xff;
   ulCheckSum += (ulStart >> 24) & 0xff;

   usFileWriteReturn = DosWrite (hFileO,
                                 &chRecordType,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &usRecordLength,
                                 (ULONG) 2,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &usSegment,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h segment: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &ulStart,
                                 (ULONG) 4,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h offset: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 pchArea + ulStart,
                                 (ULONG) ulSize,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h Data area: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   pchArea = pchArea + ulStart;
   while (ulSize--)
      ulCheckSum += *pchArea++;

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing A1h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteModuleEndRecord(void)
  {
     ULONG ulCheckSum;

     if (MainOption)
     {
        usFileWriteReturn = DosWrite (hFileO,
                                      "\213\11\0\301\0\1\1",
                                      (ULONG) 7,
                                      &ulBytesWritten);

        usFileWriteReturn = DosWrite (hFileO,
                                      &MainEntryAddress,
                                      (ULONG) 4,
                                      &ulBytesWritten);

        ulCheckSum = 0x157;
        ulCheckSum += MainEntryAddress & 0xff;
        ulCheckSum += (MainEntryAddress >> 8) & 0xff;
        ulCheckSum += (MainEntryAddress >> 16) & 0xff;
        ulCheckSum += (MainEntryAddress >> 24) & 0xff;
        ulCheckSum = 256 - (ulCheckSum & 0xff);

        usFileWriteReturn = DosWrite (hFileO,
                                      &ulCheckSum,
                                      (ULONG) 1,
                                      &ulBytesWritten);
     }
     else
        usFileWriteReturn = DosWrite (hFileO,
                                      "\212\2\0\0\164",
                                      (ULONG) 5,
                                      &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 8Ah Module End record: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

  }
/**************************************************************************/

void StartFixupRecord(void)
  {
   usFixupByteCount = 0;
  }

/**************************************************************************/

void QFixupByte(ULONG  ucTheByte)
  {
   ucFixupQ[usFixupByteCount++] = ucTheByte;
  }

/**************************************************************************/

void FlushFixupRecord(void)
  {USHORT     i;

   chRecordType = FIX386;
   usRecordLength = usFixupByteCount + 1;

   ulCheckSum = chRecordType + (usRecordLength & 0xFF) + ((usRecordLength >> 8) & 0xFF);

   for (i = 0;
        i < usFixupByteCount;
        i++)
      ulCheckSum += ucFixupQ[i];

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                                 &chRecordType,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 9Dh record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &usRecordLength,
                                 (ULONG) 2,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 9Dh record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 ucFixupQ,
                                 (ULONG) usFixupByteCount,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 9Dh fixup array: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                                 &ulCheckSum,
                                 (ULONG) 1,
                                 &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing 9Dh check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

  }

/**************************************************************************/

void FixupTextChunk (CHAR * pchReloArea,
                     ULONG  lReloLength,
                     ULONG  ulStart,
                     ULONG  ulNext)

  {USHORT                 usAnyFixups;
   USHORT                 usChunkOffset;
   USHORT                 i;
   struct OReloInfo *     ReloInfo;

   ReloInfo = (struct OReloInfo *) pchReloArea;

   /*  Are there any fuxups in this chunk ?? */
   usAnyFixups = 0;
   for (i = 0;
        i < lReloLength / 8;
        i++)
      {
       if (ReloInfo[i].Address >= ulStart &&
           ReloInfo[i].Address <  ulNext)
         usAnyFixups = 1;
      }

   if (usAnyFixups == 0)
     return;

   StartFixupRecord();


   for (i=0;
        i < lReloLength/8;
        i++)
      {if (ReloInfo[i].Address >= ulStart &&
           ReloInfo[i].Address <  ulNext)

         {
	  usChunkOffset = ReloInfo[i].Address - ulStart;

          if (ReloInfo[i].External)
            {
             switch (Symbol[ReloInfo[i].SymNumber].chType)
               {
                case EXTERIOR:
                      if (Symbol[ReloInfo[i].SymNumber].lValue ||
                          ReloInfo[i].PCRelative == 0)
                        {
                         QFixupByte (0xE4 | ((usChunkOffset & 0x300) >> 8));
                         QFixupByte (usChunkOffset & 0xff);
                         QFixupByte (0x96);
#ifdef DEBUG
			 printf("Emmiting EXTERIOR a.out ordinal #%d as omf #%d\n",
				ReloInfo[i].SymNumber, 
				symbol_ordinal_xlate[ReloInfo[i].SymNumber]);
#endif
                         if (symbol_ordinal_xlate[ReloInfo[i].SymNumber] < 0x7F)
                           {
                            QFixupByte (symbol_ordinal_xlate[ReloInfo[i].SymNumber] + 1);
                           }
                         else
                           {
                            QFixupByte (0x80 | ((symbol_ordinal_xlate[ReloInfo[i].SymNumber]+1) >> 8));
                            QFixupByte ((symbol_ordinal_xlate[ReloInfo[i].SymNumber] + 1) & 0xFF);
                           }
                         break;
                        }

                  case TEXT | EXTERIOR:
                      QFixupByte (0xA4 | ((usChunkOffset & 0x300) >> 8));
                      QFixupByte (usChunkOffset & 0xff);
                      QFixupByte (0x96);
#ifdef DEBUG
			 printf("Emmiting TEXT|EXTERIOR a.out ordinal #%d as omf #%d\n",
				ReloInfo[i].SymNumber, 
				symbol_ordinal_xlate[ReloInfo[i].SymNumber]);
#endif
                      if (symbol_ordinal_xlate[ReloInfo[i].SymNumber] < 0x7F)
                        {
                         QFixupByte (symbol_ordinal_xlate[ReloInfo[i].SymNumber] + 1);
                        }
                      else
                        {
                         QFixupByte (0x80 | ((symbol_ordinal_xlate[ReloInfo[i].SymNumber]+1) >> 8));
                         QFixupByte ((symbol_ordinal_xlate[ReloInfo[i].SymNumber] + 1) & 0xFF);
                        }
                      break;

                  case DATA:
                  case DATA | EXTERIOR:
                  case BSS:
                  default:
                      printf ("Found unimplemented named text fixup %X\n",
                              Symbol[ReloInfo[i].SymNumber].chType);
                 }
              }

          else
              /**********************************************************/
              /*  Doing un-named fixups                                 */
              /**********************************************************/
              {switch (ReloInfo[i].SymNumber & 0xE)
                 {case DATA:
#ifdef DEBUG
		      printf("Emmiting DATA fixup\n");
#endif
                      QFixupByte (0xE4 | ((usChunkOffset & 0x300) >> 8));
                      QFixupByte (usChunkOffset & 0xFF);
                      QFixupByte (0x9D);
                      break;

                  case BSS:
                      QFixupByte (0xE4 | ((usChunkOffset & 0x300) >> 8));
                      QFixupByte (usChunkOffset & 0xFF);
                      QFixupByte (0x96);
#ifdef DEBUG
			 printf("Emmiting BSS a.out ordinal #%d as omf #%d\n",
				ulBSSStartSymbol,
				symbol_ordinal_xlate[ulBSSStartSymbol]);
#endif
                      if (symbol_ordinal_xlate[ulBSSStartSymbol] < 0x7F)
                        {
                         QFixupByte (symbol_ordinal_xlate[ulBSSStartSymbol] 
							  + 1);
                        }
                      else
                        {
                         QFixupByte (0x80 | ((symbol_ordinal_xlate[ulBSSStartSymbol] + 1) >> 8));
                         QFixupByte ((symbol_ordinal_xlate[ulBSSStartSymbol]
				      + 1) & 0xFF);
                        }
                      break;

                  case TEXT:
#ifdef DEBUG
		      printf("Emmiting TEXT fixup\n");
#endif
                      QFixupByte (0xE4 | ((usChunkOffset & 0x300) >> 8));
                      QFixupByte (usChunkOffset & 0xFF);
                      QFixupByte (0x9E);
                      break;

                  default:
                      printf ("Found unimplemented unnamed text fixup %X\n",
                              Symbol[ReloInfo[i].SymNumber].chType);
                 }
              }
         }
      }

   FlushFixupRecord();

  }

/**************************************************************************/

void WriteCommunalNamesDefRecord (PSZ      pszSymbol,
                                  USHORT   usTypeIndex,
                                  ULONG    ulSize)
  {/*--------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   UCHAR  ucVariableSize;
   /*--------------------------------------------------------------------*/

   if (ulSize < 128)
       {ucVariableSize = 1;
        ulCheckSum = 0;
       }
   else
       if (ulSize < 0x100)
           {ucVariableSize = 3;
            ulCheckSum = 0x81;
           }
       else
           if (ulSize < 0x10000)
               {ucVariableSize = 4;
                ulCheckSum = 0x84;
               }
           else
               {ucVariableSize = 5;
                ulCheckSum = 0x88;
               }

   chRecordType = COMDEF;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 4 + ucVariableSize);

   ulCheckSum += chRecordType +
                 usRecordLength +
                 usTypeIndex +
                 strlen(pszSymbol) +
                 0x62;

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum += ulSize & 0xff;
   ulCheckSum += (ulSize >> 8) & 0xff;
   ulCheckSum += (ulSize >> 16) & 0xff;
   ulCheckSum += (ulSize >> 24) & 0xff;

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usTemp = (USHORT) (strlen (pszSymbol));

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               "\142",
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h data segment type: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   switch (ucVariableSize)
     {
      case 3:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\201",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

      case 4:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\204",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

      case 5:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\210",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

     }


   usFileWriteReturn = DosWrite (hFileO,
                               &ulSize,
                               (ULONG) ucVariableSize,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h communal length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteLocalCommunalNamesDefRecord (PSZ      pszSymbol,
                                       USHORT   usTypeIndex,
                                       ULONG    ulSize)
  {/*--------------------------------------------------------------------*/
   USHORT usTemp;
   USHORT x;
   UCHAR  ucVariableSize;
   /*--------------------------------------------------------------------*/

   if (ulSize < 128)
       {ucVariableSize = 1;
        ulCheckSum = 0;
       }
   else
       if (ulSize < 0x100)
           {ucVariableSize = 3;
            ulCheckSum = 0x81;
           }
       else
           if (ulSize < 0x10000)
               {ucVariableSize = 4;
                ulCheckSum = 0x84;
               }
           else
               {ucVariableSize = 5;
                ulCheckSum = 0x88;
               }

   chRecordType = LOCCOM;
   usRecordLength = (USHORT) (strlen (pszSymbol) + 4 + ucVariableSize);

   ulCheckSum += chRecordType +
                 usRecordLength +
                 usTypeIndex +
                 strlen(pszSymbol) +
                 0x62;

   for (x = 0;
        x < strlen (pszSymbol);
        x++)
      ulCheckSum += pszSymbol[x];

   ulCheckSum += ulSize & 0xff;
   ulCheckSum += (ulSize >> 8) & 0xff;
   ulCheckSum += (ulSize >> 16) & 0xff;
   ulCheckSum += (ulSize >> 24) & 0xff;

   ulCheckSum = 256 - (ulCheckSum & 0xff);

   usFileWriteReturn = DosWrite (hFileO,
                               &chRecordType,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usRecordLength,
                               (ULONG) 2,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h record header: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usTemp = (USHORT) (strlen (pszSymbol));

   usFileWriteReturn = DosWrite (hFileO,
                               &usTemp,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h name length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               pszSymbol,
                               (ULONG) strlen (pszSymbol),
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h name: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &usTypeIndex,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h type index: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               "\142",
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h data segment type: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   switch (ucVariableSize)
     {
      case 3:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\201",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

      case 4:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\204",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

      case 5:
          usFileWriteReturn = DosWrite (hFileO,
                                        "\210",
                                        (ULONG) 1,
                                        &ulBytesWritten);

          if (usFileWriteReturn)
            {printf ("Error Writing B0h length indicator: %ld\n",
                     usFileWriteReturn);
             exit(1);
           }
           ucVariableSize--;
           break;

     }


   usFileWriteReturn = DosWrite (hFileO,
                               &ulSize,
                               (ULONG) ucVariableSize,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h communal length: %ld\n",
              usFileWriteReturn);
      exit(1);
     }

   usFileWriteReturn = DosWrite (hFileO,
                               &ulCheckSum,
                               (ULONG) 1,
                               &ulBytesWritten);

   if (usFileWriteReturn)
     {printf ("Error Writing B0h check sum: %ld\n",
              usFileWriteReturn);
      exit(1);
     }
  }

/**************************************************************************/

void WriteStackSegment()
{
   usFileWriteReturn = DosWrite (hFileO,
                               "\243\24\0\5\0\0\0\0\0\20\0\0\1\0\1\0\0\0\0\0\1\0\61",
                               (ULONG) 23,
                               &ulBytesWritten);
}

/**************************************************************************/

void safe_write(unsigned char *data, unsigned int length)
{
    unsigned int rc;
    unsigned int written;
    rc = DosWrite(hFileO, data, length, &written);
    if (rc || written != length) {
	printf("Error writing to OMF file\n");
	exit(1);
    }
}

void WriteModuleLineNumbersBlock(char *buf, int buflen)
{
    static unsigned char line_header[] = 
    {
	LINNUM, 
	0,0,	/* Length */
	0,	/* Base group */
	0x01	/* Base segment */
	};

    unsigned short rec_size;
    unsigned char chksum;
    int i;

    rec_size = buflen + sizeof(line_header) - 2;
    line_header[1] = (unsigned char) rec_size;
    line_header[2] = (unsigned char) (rec_size >> 8);
    chksum = 0;
    for (i=0; i < sizeof(line_header); i++)
	chksum -= line_header[i];
    for (i=0; i < buflen; i++)
	chksum -= buf[i];
    safe_write(line_header, sizeof(line_header));
    safe_write(buf, buflen);
    safe_write(&chksum, 1);
}


void WriteLextdef(const char *symbol)
{
#if 0
    unsigned char symlen = (unsigned short) strlen(symbol);
    unsigned short reclen = symlen + 3;
    unsigned char header[] = { 
	LEXTDEF, 
	(unsigned char) reclen,
	(unsigned char) (reclen >> 8),
	symlen
	};
    unsigned char endblock[] = { 0, 0 };
    unsigned char chksum;
    int i;

    chksum = 0;
    for (i=0; i < sizeof(header); i++) chksum -= header[i];
    for (i=0; i < symlen; i++) chksum -= symbol[i];
    endblock[1] = chksum;
    safe_write(header, sizeof(header));
    safe_write(symbol, symlen);
    safe_write(endblock, sizeof(endblock));
#endif
}

void WriteLpubdef(const char *symbol, unsigned int address)
{
}

void WriteModuleLineNumbers()
{
    int sym;
    char *bp;
    char buf[6 * 19];
    unsigned long line_number, line_number_offset;

    if (ulNumberOfSymbols == 0) return;

    bp = buf;
    for (sym=0; sym < ulNumberOfSymbols; sym++) {
	switch(Symbol[sym].chType) {
	case N_SLINE:
	    if (bp >= buf + sizeof(buf)) {
		WriteModuleLineNumbersBlock(buf, sizeof(buf));
		bp = buf;
	    }

	    /* Check that the line number record looks legit */
	    if (Symbol[sym].ulIndex != 0 || Symbol[sym].chOther != 0) {
		printf("Illegal line number record\n");
		exit(1);
	    }

	    /* Add this line number record to the buf */
	    line_number = Symbol[sym].sDesc;
	    line_number_offset = Symbol[sym].lValue;
#ifdef DEBUG
	    printf("Line info:  line=%d,  address=0x%X\n",
		   line_number, line_number_offset);
#endif
	    *bp++ = (unsigned char) line_number;
	    *bp++ = (unsigned char) (line_number >> 8);
	    *bp++ = (unsigned char) line_number_offset;
	    *bp++ = (unsigned char) (line_number_offset >> 8);
	    *bp++ = (unsigned char) (line_number_offset >> 16);
	    *bp++ = (unsigned char) (line_number_offset >> 24);
	    break;
	case N_SO:
	    if (bp > buf) WriteModuleLineNumbersBlock(buf, bp-buf);
	    bp = buf;
#ifdef DEBUG
	    printf("Source file: ``%s''\n",
		   &pszSymbolTable[Symbol[sym].ulIndex-4]);
#endif
	    WriteTranslatorHeaderRecord(&pszSymbolTable[Symbol[sym]
							.ulIndex-4]);
	    break;
	case N_STSYM:
#ifdef DEBUG
	    printf("static symbol: ``%s'' at address %08X\n",
		   &pszSymbolTable[Symbol[sym].ulIndex-4],
		   Symbol[sym].lValue);
#endif
	    WriteLextdef(&pszSymbolTable[Symbol[sym].ulIndex-4]);
	    WriteLpubdef(&pszSymbolTable[Symbol[sym].ulIndex-4],
			 Symbol[sym].lValue);
	    break;
	}
    }

    if (bp > buf) WriteModuleLineNumbersBlock(buf, bp-buf);
}

void DosClose(int hFileO){
close (hFileO);
}
ULONG DosWrite(int hFileO,char *chRecordType,ULONG size,ULONG ulBytesWritten ) {
int rc;
rc=write(hFileO,chRecordType,size);
ulBytesWritten=rc;
if(rc>0)
	return 0;
else
	return -1;

//we should never get here but.. you know return types
return -1;
}
