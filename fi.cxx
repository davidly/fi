#define UNICODE

#include <stdio.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>
#include <ctype.h>
#include <excpt.h>
#include <ppl.h>

#include <mutex>
#include <chrono>

#include <windows.h>

using namespace concurrency;
using namespace std;
using namespace std::chrono;

#include <djlsav.hxx>

std::mutex mtxGlobal;
bool g_fCompression = FALSE;
bool g_fPlaceholder = FALSE;
bool g_fOnlyShowPlaceholders = FALSE;
bool g_fIgnorePlaceholders = FALSE;
bool g_fExtensions = FALSE;
bool g_fExtensionsByCount = FALSE;
bool g_fQuiet = FALSE;
bool g_fShallow = FALSE;
bool g_fFullData = FALSE;
bool g_fTotalsOnly = FALSE;
bool g_fOneThread = FALSE;
bool g_fUsage = FALSE; // disk space usage of each of top level directories under the root of the request.
HANDLE g_hConsole = INVALID_HANDLE_VALUE;

LONGLONG g_llMinSize = -1;
NUMBERFMT g_NumberFormat;

int filter( unsigned int code, struct _EXCEPTION_POINTERS *ep )
{
    printf( "exception code %#x\n", code );

    printf( " code %#x\n", ep->ExceptionRecord->ExceptionCode );
    printf( " flags %#x\n", ep->ExceptionRecord->ExceptionFlags );
    printf( " address %p\n", ep->ExceptionRecord->ExceptionAddress );

    return EXCEPTION_EXECUTE_HANDLER;
} //filter

int RenderLL( LONGLONG ll, WCHAR * pwcBuf, ULONG cwcBuf )
{
    WCHAR awc[100];
    swprintf( awc, L"%I64u", ll );

    if ( 0 != cwcBuf )
        *pwcBuf = 0;

    return GetNumberFormat( LOCALE_USER_DEFAULT, 0, awc, &g_NumberFormat, pwcBuf, cwcBuf );
} //RenderLL

class ExtensionEntry
{
    public:

        ExtensionEntry( WCHAR * pwcExt, LONGLONG cb )
        {
            pwcExtension = wcsdup( pwcExt );
            wcslwr( pwcExtension );
    
            cExtension = 1;
            cbExtension = cb;
            pLeft = NULL;
            pRight = NULL;
        }
    
        WCHAR * Extension() { return pwcExtension; }
        ExtensionEntry * Left() { return pLeft; }
        ExtensionEntry * Right() { return pRight; }
        void SetLeft( ExtensionEntry * p ) { pLeft = p; }
        void SetRight( ExtensionEntry * p ) { pRight = p; }
        LONGLONG Count() { return cExtension; }
        LONGLONG Bytes() { return cbExtension; }
    
        void Bump( LONGLONG cb )
        {
            cExtension++;
            cbExtension += cb;
        }

    private:

        WCHAR *          pwcExtension;
        LONGLONG         cExtension;
        LONGLONG         cbExtension;
        ExtensionEntry * pLeft;
        ExtensionEntry * pRight;
};

class ExtensionSet
{
    public:

        ExtensionSet()
        {
            pRoot = NULL;
        }

        void Add( ExtensionEntry * pEntry )
        {
            if ( NULL == pRoot )
            {
                pRoot = pEntry;
                return;
            }

            ExtensionEntry * pCurrent = pRoot;

            do
            {
                int cmp = wcscmp( pCurrent->Extension(), pEntry->Extension() );

                if ( cmp > 0 )
                {
                    if ( NULL == pCurrent->Left() )
                    {
                        pCurrent->SetLeft( pEntry );
                        return;
                    }

                    pCurrent = pCurrent->Left();
                }
                else if ( cmp < 0 )
                {
                    if ( NULL == pCurrent->Right() )
                    {
                        pCurrent->SetRight( pEntry );
                        return;
                    }

                    pCurrent = pCurrent->Right();
                }
                else
                {
                    printf( "adding a duplicate '%ws'!\n", pEntry->Extension() );
                    exit( 1 );
                }
            } while ( TRUE );
        }

        static int EntryCompareSize( const void * a, const void * b )
        {
            ExtensionEntry *pa = *(ExtensionEntry **) a;
            ExtensionEntry *pb = *(ExtensionEntry **) b;

            LONGLONG diff = pa->Bytes() - pb->Bytes();

            if ( diff > 0 )
                return 1;

            if ( 0 == diff )
                return 0;

            return -1;
        }

        static int EntryCompareCount( const void * a, const void * b )
        {
            ExtensionEntry *pa = *(ExtensionEntry **) a;
            ExtensionEntry *pb = *(ExtensionEntry **) b;

            LONGLONG diff = pa->Count() - pb->Count();

            if ( diff > 0 )
                return 1;

            if ( 0 == diff )
                return 0;

            return -1;
        }

        void PrintSorted()
        {
            int cEntries = Count( pRoot );

            if ( 0 == cEntries )
                return;

            ExtensionEntry ** aEntries = new ExtensionEntry * [ cEntries ];

            int iSoFar = 0;

            FillArray( aEntries, iSoFar, pRoot );

            if ( g_fExtensionsByCount )
                qsort( aEntries, cEntries, sizeof( ExtensionEntry * ), EntryCompareCount );
            else
                qsort( aEntries, cEntries, sizeof( ExtensionEntry * ), EntryCompareSize );

            for ( int i = 0; i < cEntries; i++ )
            {
                ExtensionEntry * p = aEntries[ i ];
                RenderLL( p->Bytes(), awc, _countof( awc ) - 1 );
                RenderLL( p->Count(), awc2, _countof( awc2 ) - 1 );
                wprintf( L"%19ws %12ws  %ws\n", awc, awc2, p->Extension() );
            }

            delete [] aEntries;
        } //PrintSortedBySize

        ExtensionEntry * Find( WCHAR * pwc )
        {
            ExtensionEntry *p = pRoot;

            do
            {
                if ( NULL == p )
                    return NULL;

                int cmp = wcsicmp( pwc, p->Extension() );

                if ( 0 == cmp )
                    return p;

                if ( cmp < 0 )
                    p = p->Left();
                else
                    p = p->Right();
            } while ( TRUE );
        }

        void Print()
        {
            PrintInternal( pRoot );
        }

    private:

        void FillArray( ExtensionEntry ** aEntries, int &i, ExtensionEntry *p )
        {
            if ( NULL == p )
                return;

            aEntries[ i++ ] = p;

            FillArray( aEntries, i, p->Left() );
            FillArray( aEntries, i, p->Right() );
        }

        int Count( ExtensionEntry * p )
        {
            if ( NULL == p )
                return 0;

            return 1 + Count( p->Left() ) + Count( p->Right () );
        }

        void PrintInternal( ExtensionEntry * p )
        {
            if ( NULL == p )
                return;

            PrintInternal( p->Left() );

            RenderLL( p->Bytes(), awc, _countof( awc ) - 1 );
            RenderLL( p->Count(), awc2, _countof( awc2 ) - 1 );
            wprintf( L"%19ws %12ws %40ws\n", awc, awc2, p->Extension() );

            PrintInternal( p->Right() );
        }

        ExtensionEntry * pRoot;
        WCHAR awc[ 100 ];
        WCHAR awc2[ 100 ];
};

ExtensionSet g_ExtensionSet;

void RenderST( SYSTEMTIME &st )
{
    BOOL pm = st.wHour >= 12;
    
    if ( st.wHour > 12 )
        st.wHour -= 12;
    else if ( 0 == st.wHour )
        st.wHour = 12;
       
    wprintf( L"%2d-%02d-%04d %2d:%02d:%02d%wc  ",
             (DWORD) st.wMonth,
             (DWORD) st.wDay,
             (DWORD) st.wYear,
             (DWORD) st.wHour,
             (DWORD) st.wMinute,
             (DWORD) st.wSecond,
             pm ? L'p' : L'a');
} //RenderST

void PrintFT( FILETIME ftIn )
{
    SYSTEMTIME stGMT;
    FileTimeToSystemTime(&ftIn,&stGMT);

    RenderST( stGMT );
    printf( "UTC " );

    FILETIME ft;
    FileTimeToLocalFileTime( &ftIn, &ft );
    
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft,&st);

    RenderST( st );
    printf( "local " );
} //PrintFT

void PrintFTLocal( FILETIME ftIn )
{
    FILETIME ft;
    FileTimeToLocalFileTime( &ftIn, &ft );
    
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft,&st);

    RenderST( st );
} //PrintFTLocal

void PrintAttrib( DWORD a )
{
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_ARCHIVE ) ) ? L'a' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_COMPRESSED ) ) ? L'c' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_DIRECTORY ) ) ? L'd' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_ENCRYPTED ) ) ? L'e' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_HIDDEN ) ) ? L'h' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED ) ) ? L'i' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_INTEGRITY_STREAM ) ) ? L'I' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_NORMAL ) ) ? L'n' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_OFFLINE ) ) ? L'o' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_RECALL_ON_OPEN ) ) ? L'O' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_REPARSE_POINT ) ) ? L'p' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_READONLY ) ) ? L'r' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ) ) ? L'R' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_SYSTEM ) ) ? L's' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_SPARSE_FILE ) ) ? L'S' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_TEMPORARY ) ) ? L't' : L' ' );
    putwchar( ( 0 != ( a & FILE_ATTRIBUTE_VIRTUAL ) ) ? L'v' : L' ' );
} //PrintAttrib

LONGLONG CompressedSize( const WCHAR * pwcPath )
{
    DWORD dwHigh = 0;
    DWORD dwLow = GetCompressedFileSize( pwcPath, &dwHigh );

    return ( ( (LONGLONG) dwHigh ) << 32 ) + dwLow;
} //CompressedSize

void FullData( const WIN32_FIND_DATA & fd, const WCHAR * pwcPath, LONGLONG & llFileSize )
{
    // wprintf( L"%ws%ws\n", pwcPath, fd.cFileName );
    WriteConsole( g_hConsole, pwcPath, wcslen( pwcPath ), 0, 0 );
    WriteConsole( g_hConsole, fd.cFileName, wcslen( fd.cFileName ), 0, 0 );
    wprintf( L"\n" );

    //wprintf( L"  attrib %#x, reserved0: %#x\n", fd.dwFileAttributes, fd.dwReserved0 );

    wprintf( L"  attributes:       " );
    PrintAttrib( fd.dwFileAttributes );
    wprintf( L"\n" );

    wprintf( L"  create time:      " );
    PrintFTLocal( fd.ftCreationTime );
    wprintf( L"\n" );

    wprintf( L"  last access time: " );
    PrintFTLocal( fd.ftLastAccessTime );
    wprintf( L"\n" );

    wprintf( L"  last write time:  " );
    PrintFTLocal( fd.ftLastWriteTime );
    wprintf( L"\n" );

    LONGLONG llSize = ( ( (LONGLONG) fd.nFileSizeHigh ) << 32 ) + (LONGLONG) fd.nFileSizeLow;
    wprintf( L"  size:             %I64u\n", llSize );

    InterlockedExchangeAdd64( &llFileSize, llSize );

    //wprintf( L"  dwReserved0:   %#x\n", fd.dwReserved0 );
    //wprintf( L"  dwReserved1:   %#x\n", fd.dwReserved1 );
    //wprintf( L"  alternatename: %ws\n", fd.cAlternateFileName );

    printf( "\n" );
} //FullData

void AppendBackslashAndLowercase( WCHAR * pwc )
{
    _wcslwr( pwc );

    int i = wcslen( pwc );

    if ( ( i > 0 ) && ( L'\\' != pwc[ i - 1 ] ) )
    {
        pwc[ i++ ] = L'\\';
        pwc[ i ] = 0;
    }
} //AppendBackslash   
        
void DoScope( const WCHAR * pwcPath, const WCHAR * pwcSpec, int iDepth, LONGLONG & llBytesTotal, LONGLONG & llCompressedBytesTotal,
              LONGLONG & llPlaceholderBytesTotal, LONGLONG & llCountOfFiles, LONGLONG & llCountOfDirectories )
{
    WCHAR awcTmp[ MAX_PATH ];
    const int cwcTmp = _countof( awcTmp ) - 1;

    LONGLONG llLocalBytesTotal = 0;
    LONGLONG llLocalCompressedBytesTotal = 0;
    LONGLONG llLocalPlaceholderBytesTotal = 0;
    LONGLONG llLocalCountOfFiles = 0;
    LONGLONG llLocalCountOfDirectories = 0;

    int pathLen = wcslen( pwcPath );
    int specLen = wcslen( pwcSpec );

    WCHAR awcSpec[ MAX_PATH ];
    if ( ( pathLen + specLen ) >= _countof( awcSpec ) )
        return;

    wcscpy( awcSpec, pwcPath );
    wcscpy( awcSpec + pathLen, pwcSpec );

    WIN32_FIND_DATA fd;
    HANDLE hFile = FindFirstFileEx( awcSpec, FindExInfoBasic, &fd, FindExSearchNameMatch, 0, FIND_FIRST_EX_LARGE_FETCH );

    if ( INVALID_HANDLE_VALUE != hFile )
    {
        do
        {
            if ( ( 0 != wcscmp( fd.cFileName, L"." ) ) &&
                 ( 0 != wcscmp( fd.cFileName, L".." ) ) )
            {
                LONGLONG llSize = ( ( (LONGLONG) fd.nFileSizeHigh ) << 32 ) + (LONGLONG) fd.nFileSizeLow;
                if ( llSize < g_llMinSize )
                    continue;

                DWORD a = fd.dwFileAttributes;
                bool isPlaceholder = ( 0 != ( FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS & a ) );

                if ( 0 == ( a & FILE_ATTRIBUTE_DIRECTORY ) )
                {
                    if ( ( isPlaceholder && g_fIgnorePlaceholders ) || ( !isPlaceholder && g_fOnlyShowPlaceholders ) )
                        continue;

                    int nameLen = wcslen( fd.cFileName );
                    if ( ( pathLen + nameLen ) >= _countof( awcSpec ) )
                        continue;

                    InterlockedIncrement64( &llLocalCountOfFiles );

                    if ( g_fExtensions )
                    {
                        WCHAR * pwcPeriod = wcsrchr( fd.cFileName, L'.' );

                        if ( NULL != pwcPeriod )
                        {
                            pwcPeriod++;

                            lock_guard<mutex> lock( mtxGlobal );

                            ExtensionEntry * pEntry = g_ExtensionSet.Find( pwcPeriod );

                            if ( NULL == pEntry )
                            {
                                pEntry = new ExtensionEntry( pwcPeriod, llSize );
                                g_ExtensionSet.Add( pEntry );
                            }
                            else
                            {
                                pEntry->Bump( llSize );
                            }
                        }
                    }
                }

                lock_guard<mutex> lock( mtxGlobal );

                if ( !g_fQuiet && !g_fFullData )
                {
                    InterlockedExchangeAdd64( &llLocalBytesTotal, llSize );
        
                    if ( !g_fTotalsOnly && !g_fExtensions )
                    {
                        PrintAttrib( a );
                        RenderLL( llSize, awcTmp, cwcTmp );
                        wprintf( L"%17ws  ", awcTmp );
                    }

                    if ( g_fCompression )
                    {
                        LONGLONG llCompressedSize = llSize;

                        if ( ( 0 != ( FILE_ATTRIBUTE_COMPRESSED & a ) ) ||
                             ( 0 != ( FILE_ATTRIBUTE_SPARSE_FILE & a ) ) )
                        {
                            wcscpy( awcTmp, pwcPath );
                            wcscpy( awcTmp + pathLen, fd.cFileName );
                            llCompressedSize = CompressedSize( awcTmp );
                        }

                        if ( !g_fTotalsOnly && !g_fExtensions )
                        {
                            RenderLL( llCompressedSize, awcTmp, cwcTmp );
                            wprintf( L"%17ws  ", awcTmp );
                        }

                        InterlockedExchangeAdd64( &llLocalCompressedBytesTotal, llCompressedSize );
                    }

                    if ( g_fPlaceholder )
                    {
                        LONGLONG llPlaceholderSize = 0;

                        if ( isPlaceholder )
                             llPlaceholderSize = llSize;

                        if ( !g_fTotalsOnly && !g_fExtensions )
                        {
                            RenderLL( llPlaceholderSize, awcTmp, cwcTmp );
                            wprintf( L"%17ws  ", awcTmp );
                        }

                        InterlockedExchangeAdd64( &llLocalPlaceholderBytesTotal, llPlaceholderSize );
                    }

                    if ( !g_fTotalsOnly && !g_fExtensions )
                        PrintFTLocal( fd.ftLastWriteTime );
                }
    
                if ( g_fFullData )
                {
                    FullData( fd, pwcPath, llLocalBytesTotal );
                }
                else if ( !g_fTotalsOnly && !g_fExtensions )
                {
                    _wcslwr( fd.cFileName );

                    //wprintf( L"%ws%ws\n", pwcPath, fd.cFileName );
                    WriteConsole( g_hConsole, pwcPath, wcslen( pwcPath ), 0, 0 );
                    WriteConsole( g_hConsole, fd.cFileName, wcslen( fd.cFileName ), 0, 0 );
                    wprintf( L"\n" );
                }
            }
        } while ( FindNextFile( hFile, &fd ) );

        FindClose( hFile );
    }

    if ( g_fShallow )
        return;

    // Now look for directories

    wcscpy( awcSpec, pwcPath );
    wcscpy( awcSpec + pathLen, L"*" );

    CStringArray aDirs;
                                               
    hFile = FindFirstFileEx( awcSpec, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, 0, FIND_FIRST_EX_LARGE_FETCH );

    if ( INVALID_HANDLE_VALUE != hFile )
    {
        do
        {
            if ( ( 0 != ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) &&
                 ( 0 != wcscmp( fd.cFileName, L".") ) &&
                 ( 0 != wcscmp( fd.cFileName, L"..") ) )
            {
                int fileLen = wcslen( fd.cFileName );

                if ( ( pathLen + fileLen + 1 ) >= _countof( awcSpec ) )
                    continue;

                wcscpy( awcSpec + pathLen, fd.cFileName );
                AppendBackslashAndLowercase( awcSpec + pathLen );

                aDirs.Add( awcSpec );
            }
        } while ( FindNextFile( hFile, &fd ) );

        FindClose( hFile );
    }

    if ( 0 != aDirs.Count() )
    {
        if ( g_fOneThread )
        {
            for ( int i = 0; i < aDirs.Count(); i++ )
            {
                try
                {
                    DoScope( aDirs[ i ], pwcSpec, iDepth + 1, llLocalBytesTotal, llLocalCompressedBytesTotal,
                             llLocalPlaceholderBytesTotal, llLocalCountOfFiles, llLocalCountOfDirectories );
                    InterlockedIncrement64( &llLocalCountOfDirectories );
                }
                catch( ... )
                {
                    wprintf( L"caught exception in single-threaded processing subfolders\n" );
                }
            }
        }
        else
        {
            parallel_for ( 0, (int) aDirs.Count(), [&]( int i )
            {
                try
                {
                    DoScope( aDirs[ i ], pwcSpec, iDepth + 1, llLocalBytesTotal, llLocalCompressedBytesTotal,
                             llLocalPlaceholderBytesTotal, llLocalCountOfFiles, llLocalCountOfDirectories );
                    InterlockedIncrement64( &llLocalCountOfDirectories );
                }
                catch( ... )
                {
                    wprintf( L"caught exception in parallel_for processing subfolders\n" );
                }
            } );
        }
    }

    InterlockedExchangeAdd64( &llBytesTotal, llLocalBytesTotal );
    InterlockedExchangeAdd64( &llCompressedBytesTotal, llLocalCompressedBytesTotal );
    InterlockedExchangeAdd64( &llPlaceholderBytesTotal, llLocalPlaceholderBytesTotal );
    InterlockedExchangeAdd64( &llCountOfFiles, llLocalCountOfFiles );
    InterlockedExchangeAdd64( &llCountOfDirectories, llLocalCountOfDirectories );

    // print out file usage information for this top-level directory if appropriate

    if ( g_fUsage && ( iDepth <= 1 ) )
    {
        lock_guard<mutex> lock( mtxGlobal );

        RenderLL( llLocalBytesTotal, awcTmp, cwcTmp);
        wprintf( L"%19ws  ", awcTmp );

        if ( g_fCompression )
        {
            RenderLL( llLocalCompressedBytesTotal, awcTmp, cwcTmp);
            wprintf( L"%19ws  ", awcTmp );
        }

        if ( g_fPlaceholder )
        {
            RenderLL( llLocalPlaceholderBytesTotal, awcTmp, cwcTmp);
            wprintf( L"%19ws  ", awcTmp );
        }

        RenderLL( llLocalCountOfFiles, awcTmp, cwcTmp);
        wprintf( L"%14ws  ", awcTmp );

        //wprintf( L"%ws\n", pwcPath );
        WriteConsole( g_hConsole, pwcPath, wcslen( pwcPath ), 0, 0 );
        wprintf( L"\n" );
    }
} //DoScope

void Usage( const WCHAR * pwcApp )
{
    wprintf( L"usage: %ws [-c] [-d:path] [-e] [-f] [-o] [-q] [-r] [-s] [filespec]\n", pwcApp );
    wprintf( L"\n" );
    wprintf( L"    -c           compression information is shown. Default No.\n" );
    wprintf( L"    -d:path      directory at the root of the enumeration. Default is root.\n" );
    wprintf( L"    -e           summary view organized by file extensions sorted by size. -ec to sort by count.\n" );
    wprintf( L"    -f           full file information. Default No.\n" );
    wprintf( L"    -m:X         only show files >= X bytes.\n" );
    wprintf( L"    -o           one thread only. slower, but results in alpha order.\n" );
    wprintf( L"    -p           placeholder (onedrive stub) information is shown. Default No.\n" );
    wprintf( L"    -q           quiet output -- display just paths. Default No.\n" );
    wprintf( L"    -s           shallow traversal -- don't walk subdirectories. Default No.\n" );
    wprintf( L"    -t           total(s) only. Default No.\n" );
    wprintf( L"    -u           usage of top level directories report. Default No.\n" );
    wprintf( L"    filespec     filename or wildcard to search for.  Default is *\n" );
    wprintf( L"\n" );
    wprintf( L"    alternate usages: %ws [-c] [-q] [-r] [-s] path filespec\n", pwcApp );
    wprintf( L"                      %ws [-c] [-q] [-r] [-s] path\n", pwcApp );
    wprintf( L"\n" );
    wprintf( L"    The Placholder argument [-p] can have one of three values:\n" );
    wprintf( L"          -p   Show placeholder top-level usage\n" );
    wprintf( L"          -po  Only consider placeholder files\n" );
    wprintf( L"          -pi  Ignore all placeholder files\n" );
    wprintf( L"\n" );
    wprintf( L"    examples:\n" );
    wprintf( L"        %ws -d:c:\\foo -s *.wma\n", pwcApp );
    wprintf( L"        %ws . -s *.wma\n", pwcApp );
    wprintf( L"        %ws *.wma\n", pwcApp );
    wprintf( L"        %ws /m:1000000000 *.vhdx\n", pwcApp );
    wprintf( L"        %ws .. -u * -t\n", pwcApp );
    wprintf( L"        %ws \\\\machine\\share * -u -t\n", pwcApp );
    wprintf( L"        %ws -t -u .\n", pwcApp );
    wprintf( L"        %ws -o -t -u .\n", pwcApp );
    wprintf( L"        %ws -e .\n", pwcApp );
    wprintf( L"\n" );
    wprintf( L"    file attributes:\n" );
    wprintf( L"        a: archive\n" );
    wprintf( L"        c: compressed\n" );
    wprintf( L"        d: directory\n" );
    wprintf( L"        e: encrypted\n" );
    wprintf( L"        h: hidden\n" );
    wprintf( L"        i: not content indexed\n" );
    wprintf( L"        I: integrity stream\n" );
    wprintf( L"        n: normal\n" );
    wprintf( L"        o: offline\n" );
    wprintf( L"        O: recall on open\n" );
    wprintf( L"        p: reparse point\n" );
    wprintf( L"        r: read only\n" );
    wprintf( L"        R: recall on data access (OneDrive placeholder)\n" );
    wprintf( L"        s: system\n" );
    wprintf( L"        S: sparse\n" ); 
    wprintf( L"        v: virtual\n" );
    exit( 1 );
} //Usage

extern "C" int __cdecl wmain( int argc, WCHAR * argv[] )
{
    __try
    {
    g_hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
    ZeroMemory( &g_NumberFormat, sizeof g_NumberFormat );
    g_NumberFormat.NumDigits = 0;
    g_NumberFormat.Grouping = 3;
    g_NumberFormat.lpDecimalSep = L".";
    g_NumberFormat.lpThousandSep = L",";

    WCHAR awcPath[ MAX_PATH ];
    awcPath[ 0 ] = 0;

    WCHAR awcSpec[ MAX_PATH ];
    awcSpec[ 0 ] = 0;

    int iArg = 1;
    while ( iArg < argc )
    {
        const WCHAR * pwcArg = argv[iArg];
        WCHAR a0 = pwcArg[0];

        if ( ( L'-' == a0 ) ||
             ( L'/' == a0 ) )
        {
           WCHAR a1 = towlower( pwcArg[1] );

           if ( L'q' == a1 )
               g_fQuiet = TRUE;
           else if ( L'c' == a1 )
               g_fCompression = TRUE;
           else if ( L'e' == a1 )
           {
               g_fExtensions= TRUE;
               if ( L'c' == pwcArg[2] )
                   g_fExtensionsByCount = TRUE;
               else if ( 0 != pwcArg[2] )
                   Usage( argv[ 0 ] );
           }
           else if ( L'f' == a1 )
               g_fFullData = TRUE;
           else if ( L'm' == a1 )
           {
               if ( L':' != pwcArg[2] )
                   Usage( argv[ 0 ] );

               g_llMinSize = _wtoll( pwcArg + 3 );
           }
           else if ( L'o' == a1 )
               g_fOneThread = TRUE;
           else if ( L'p' == a1 )
           {
               g_fPlaceholder = TRUE;

               if ( L'i' == pwcArg[2] )
                   g_fIgnorePlaceholders = true;
               else if ( L'o' == pwcArg[2] )
                   g_fOnlyShowPlaceholders = true;
               else if ( 0 != pwcArg[2] )
                   Usage( argv[ 0 ] );
           }
           else if ( L's' == a1 )
               g_fShallow = TRUE;
           else if ( L't' == a1 )
               g_fTotalsOnly = TRUE;
           else if ( L'u' == a1 )
               g_fUsage = TRUE;
           else if ( L'd' == a1 )
           {
               if ( ( 0 != awcPath[ 0 ] ) ||
                    ( ( L'\\' != pwcArg[2] ) && ( L':' != pwcArg[2] ) ) )
                   Usage( argv[ 0 ] );

               _wfullpath( awcPath, pwcArg + 3, _countof( awcPath ) );
           }
           else
               Usage( argv[ 0 ] );
        }
        else
        {
            BOOL fPath = ( ( 0 != wcschr( pwcArg, L':' ) ) ||
                           ( 0 != wcschr( pwcArg, L'\\' ) ) ||
                           ( 0 == wcscmp( pwcArg, L".." ) ) ||
                           ( 0 == wcscmp( pwcArg, L"." ) ) );

            if ( fPath )
            {
                if ( 0 != awcPath[ 0 ] )
                    Usage( argv[ 0 ] );

                _wfullpath( awcPath, pwcArg, _countof( awcPath ) );
            }
            else
            {
                if ( 0 != awcSpec[ 0 ] )
                {
                    if ( 0 != awcPath[ 0 ] )
                        Usage( argv[ 0 ] );

                    _wfullpath( awcPath, awcSpec, _countof( awcPath ) );
                }

                wcscpy( awcSpec, pwcArg );
            }
        }

       iArg++;
    }

    // default start is the root.

    if ( 0 == awcPath[ 0 ] )
         _wfullpath( awcPath, L"\\", _countof( awcPath ) );

    AppendBackslashAndLowercase( awcPath );

    // default filespec is all files

    if ( 0 == awcSpec[ 0 ] )
        wcscpy( awcSpec, L"*" );

    if ( !g_fQuiet && !g_fTotalsOnly && !g_fExtensions && ! g_fFullData )
    {
        if ( g_fCompression )
        {
            wprintf( L"attributes                   bytes         compressed                 write  path\n" );
            wprintf( L"----------                   -----         ----------                 -----  ----\n" );
        }
        else if ( g_fPlaceholder )
        {
            wprintf( L"attributes                   bytes        placeholder                 write  path\n" );
            wprintf( L"----------                   -----        -----------                 -----  ----\n" );
        }
        else
        {
            wprintf( L"attributes                   bytes                 write  path\n" );
            wprintf( L"----------                   -----                 -----  ----\n" );
        }
    }

    if ( g_fUsage )
    {
        if ( g_fCompression && g_fPlaceholder )
        {
            wprintf( L"              bytes           compressed          placeholder           files  path\n" );
            wprintf( L"              -----           ----------          -----------           -----  ----\n" );
        }
        else if ( g_fCompression )
        {
            wprintf( L"              bytes           compressed           files  path\n" );
            wprintf( L"              -----           ----------           -----  ----\n" );
        }
        else if ( g_fPlaceholder )
        {
            wprintf( L"              bytes          placeholder           files  path\n" );
            wprintf( L"              -----          -----------           -----  ----\n" );
        }
        else
        {
            wprintf( L"              bytes           files  path\n" );
            wprintf( L"              -----           -----  ----\n" );
        }
    }

    if ( g_fExtensions )
    {
        wprintf( L"              bytes        files  extension\n" );
        wprintf( L"              -----        -----  ---------\n" );
    }

    // Actually do the enumeration

    LONGLONG llBytesTotal = 0;
    LONGLONG llCompressedBytesTotal = 0;
    LONGLONG llPlaceholderBytesTotal = 0;
    LONGLONG llCountOfFiles = 0;
    LONGLONG llCountOfDirectories = 0;

    DoScope( awcPath, awcSpec, 0, llBytesTotal, llCompressedBytesTotal, llPlaceholderBytesTotal, llCountOfFiles, llCountOfDirectories );

    // Count the root folder if there was anything there

    if ( 0 != llBytesTotal || 0 != llCountOfFiles || 0 != llCountOfDirectories )
        llCountOfDirectories++;

    // Print stats about the work

    if ( g_fExtensions )
        g_ExtensionSet.PrintSorted();

    if ( !g_fQuiet )
    {
        wprintf( L"\n" );

        WCHAR awc[ 100 ];
        int cwc = _countof( awc ) - 1;

        wprintf( L"files:                 " );
        RenderLL( llCountOfFiles, awc, cwc );
        wprintf( L"%19ws\n", awc );

        wprintf( L"directories:           " );
        RenderLL( llCountOfDirectories, awc, cwc );
        wprintf( L"%19ws\n", awc );

        wprintf( L"total used bytes:      " );
        RenderLL( llBytesTotal, awc, cwc );
        wprintf( L"%19ws\n", awc );

        if ( g_fCompression )
        {
            wprintf( L"used compressed bytes: " );
            RenderLL( llCompressedBytesTotal, awc, cwc );
            wprintf( L"%19ws\n", awc );
        }

        if ( g_fPlaceholder )
        {
            wprintf( L"placeholder bytes:     " );
            RenderLL( llPlaceholderBytesTotal, awc, cwc );
            wprintf( L"%19ws\n", awc );
        }

        ULARGE_INTEGER iAvailableWithQuota, iTotal, iFree;

        if ( GetDiskFreeSpaceEx( awcPath, &iAvailableWithQuota, &iTotal, &iFree ) )
        {
            wprintf( L"free bytes:            " );
            RenderLL( iFree.QuadPart, awc, cwc );
            wprintf( L"%19ws\n", awc );

            wprintf( L"partition size:        " );
            RenderLL( iTotal.QuadPart, awc, cwc );
            wprintf( L"%19ws\n", awc );
        }
    }
    }
    __except( filter( GetExceptionCode(), GetExceptionInformation() ) )
    {
        printf( "caught exception\n" );
    }

    return 0;
} //wmain


