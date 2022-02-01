# fi
File Information. Command-line Windows app to show aggregated file counts and sizes.

usage: fi [-c] [-d:path] [-e] [-f] [-o] [-q] [-r] [-s] [filespec]

    -c           compression information is shown.  Default No.
    -d:path      directory at the root of the enumeration.  Default is root.
    -e           summary view organized by file extensions sorted by size. -ec to sort by count.
    -f           full file information.  Default No.
    -m:X         only show files >= X bytes.
    -o           one thread only. slower, but results in alpha order.
    -p           placeholder (onedrive stub) information is shown. Default No.
    -q           quiet output -- display just paths.  Default No.
    -s           shallow traversal -- don't walk subdirectories.  Default No.
    -t           total(s) only.  Default No.
    -u           usage of top level directories report.  Default No.
    filespec     filename or wildcard to search for.  Default is *

    alternate usages: fi [-c] [-q] [-r] [-s] path filespec
                      fi [-c] [-q] [-r] [-s] path

    The Placholder argument [-p] can have one of three values:
          -p   Show placeholder top-level usage
          -po  Only consider placeholder files
          -pi  Ignore all placeholder files

    examples:
        fi -d:c:\foo -s *.wma
        fi . -s *.wma
        fi *.wma
        fi /m:1000000000 *.vhdx
        fi .. -u * -t
        fi \\machine\share * -u -t
        fi -t -u .
        fi -o -t -u .
        fi -e .

    file attributes:
        a: archive
        c: compressed
        d: directory
        e: encrypted
        h: hidden
        i: not content indexed
        I: integrity stream
        n: normal
        o: offline
        O: recall on open
        p: reparse point
        r: read only
        R: recall on data access (OneDrive placeholder)
        s: system
        S: sparse
        v: virtual

Sample output:

C:\Users>fi -t -u .

                  bytes           files  path
                  -----           -----  ----
                      0               0  c:\users\default user\
            130,267,667             102  c:\users\public\
              2,699,951              84  c:\users\default\
         56,420,768,365          55,929  c:\users\all users\
        346,969,652,649         424,113  c:\users\david\
        403,523,388,806         480,229  c:\users\

    files:                             480,229
    directories:                        94,587
    total used bytes:          403,523,388,806
    free bytes:              1,412,594,487,296
    partition size:          1,989,629,681,664
    

D:\flac>fi -t -e .

                  bytes      files  extension
                  -----      -----  ---------
                 18,055          1  m3u
                 53,722          4  jpeg
              5,779,825          1  mp3
             22,916,675          1  mov
             63,229,025          4  txt
            273,563,766         67  png
            286,015,111         27  pdf
            527,377,191          3  mp4
          1,866,594,465       1559  jpg
        493,943,497,728      17971  flac

    files:                              19,638
    directories:                         2,410
    total used bytes:          496,989,045,563
    free bytes:             16,143,174,991,872
    partition size:         24,004,635,123,712
