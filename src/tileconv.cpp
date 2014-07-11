
#include <fstream>

#include "optionparser.h"
#include "smt.h"
#include "smf.h"
#include "smtool.h"
#include "util.h"
#include "tilecache.h"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

using namespace std;
OIIO_NAMESPACE_USING;

// Argument tests //
////////////////////
struct Arg: public option::Arg
{
    static void printError(const char* msg1, const option::Option& opt, const char* msg2)
    {
        fprintf(stderr, "%s", msg1);
        fwrite(opt.name, opt.namelen, 1, stderr);
        fprintf(stderr, "%s", msg2);
    }

    static option::ArgStatus Unknown(const option::Option& option, bool msg)
    {
        if (msg) printError("Unknown option '", option, "'\n");
        return option::ARG_ILLEGAL;
    }

    static option::ArgStatus Required(const option::Option& option, bool msg)
    {
        if (option.arg != 0)
            return option::ARG_OK;

        if (msg) printError("Option '", option, "' requires an argument\n");
        return option::ARG_ILLEGAL;
    }

    static option::ArgStatus Numeric(const option::Option& option, bool msg)
    {
        char* endptr = 0;
        if (option.arg != 0 && strtof(option.arg, &endptr)){};
        if (endptr != option.arg && *endptr == 0)
            return option::ARG_OK;

        if (msg) printError("Option '", option, "' requires a numeric argument\n");
        return option::ARG_ILLEGAL;
    }
};

enum optionsIndex
{
    UNKNOWN,
    // General Options
    HELP, VERBOSE, QUIET, FORCE,
    //TODO add append, overwrite, clobber, force, whatever options and scemantics.
    //Specification
    MAPSIZE,
    TILERES,
    IMAGESIZE,
    STRIDE,
    // Creation
    DECALS,
    FILTER,
    OUTPUT,
    // Compression
    DXT1_QUALITY,
    CNUM, CPET, CNET,
    // Deconstruction
    SEPARATE,
    COLLATE,
    RECONSTRUCT
};

const option::Descriptor usage[] = {
    { UNKNOWN, 0, "", "", Arg::None,
        "USAGE: tileconv [options] [source files] \n"
        "  eg. 'tileconv -v -o mysmt.smt *.jpg'\n"
        "\nGENERAL OPTIONS:" },
    { HELP, 0, "h", "help", Arg::None,
        "  -h,  \t--help  \tPrint usage and exit." },
    { VERBOSE, 0, "v", "verbose", Arg::None,
        "  -v,  \t--verbose  \tPrint extra information." },
    { QUIET, 0, "q", "quiet", Arg::None,
        "  -q,  \t--quiet  \tSupress output." },
    { FORCE, 0, "f", "force", Arg::None,
        "  -f,  \t--force  \toverwrite existing files." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nSPECIFICATIONS:" },
    { MAPSIZE, 0, "", "mapsize", Arg::Required,
        "\t--mapsize=XxY  \tWidth and length of map, in spring map units eg. '--mapsize=4x4', must be multiples of two." },
    { TILERES, 0, "", "tileres", Arg::Numeric,
        "\t--tileres=X  \tXY resolution of tiles to save, eg. '--tileres=32'." },
    { IMAGESIZE, 0, "", "imagesize", Arg::Required,
        "\t--imagesize=XxY  \tScale the resultant extraction to this size, eg. '--imagesize=1024x768'." },
    { STRIDE, 0, "", "stride", Arg::Numeric,
        "\t--stride=N  \tNumber of source tiles horizontally before wrapping." },
    
    { UNKNOWN, 0, "", "", Arg::None,
        "\nCREATION:" },
//    { DECALS, 0, "", "decals", Arg::Required,
//        "\t--decals=filename.csv  \tFile to parse when pasting decals." },
    { FILTER, 0, "", "filter", Arg::Required,
        "\t--filter=[1,2,n,1-n]  \tAppend only these tiles" },
    { OUTPUT, 0, "o", "output", Arg::Required,
        "  -o,  \t--output=filename.smt  \tfilename to output." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nCOMPRESSION OPTIONS:" },
    { DXT1_QUALITY, 0, "", "dxt1-quality", Arg::None,
        "\t--dxt1-quality  \tUse slower but better analytics when compressing DXT1 textures" },
    { CNUM, 0, "", "cnum", Arg::Numeric,
        "\t--cnum=[-1,0,N]  \tNumber of tiles to compare; n=-1, no comparison; n=0, hashtable exact comparison; n > 0, numeric comparison between n tiles" },
    { CPET, 0, "", "cpet", Arg::Numeric,
        "\t--cpet  \tPixel error threshold. 0.0f-1.0f" },
    { CNET, 0, "", "cnet=[0.0-1.0]", Arg::Numeric,
        "\t--cnet=[0-N]  \tErrors threshold 0-1024." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nDECONSTRUCTION OPTIONS:" },
    { SEPARATE, 0, "", "separate", Arg::None,
        "  \t--separate  \tSplit the source files into individual images." },
    { COLLATE, 0, "", "collate", Arg::None,
        "  \t--collate  \tCollate the extracted tiles." },
    { RECONSTRUCT, 0, "", "reconstruct", Arg::Required,
        "  \t--reconstruct=tilemap.exr  \tReconstruct the extracted tiles using a tilemap." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nEXAMPLES:\n"
        "  tileconv \n"
        "  tileconv"
    },
    { 0, 0, 0, 0, 0, 0 }
};

int
main( int argc, char **argv )
{
    argc -= (argc > 0); argv += (argc > 0);
    option::Stats stats( usage, argc, argv );
    option::Option* options = new option::Option[ stats.options_max ];
    option::Option* buffer = new option::Option[ stats.buffer_max ];
    option::Parser parse( usage, argc, argv, options, buffer );

    bool fail = false;
    for( option::Option* opt = options[ UNKNOWN ]; opt; opt = opt->next() ){
        std::cout << "Unknown option: " << std::string( opt->name,opt->namelen ) << "\n";
        fail = true;
    }
    if( fail ) exit( 1 );

    if( parse.error() ) exit( 1 );

    if( options[ HELP ] || argc == 0 ) {
        int columns = getenv( "COLUMNS" ) ? atoi( getenv( "COLUMNS" ) ) : 80;
        option::printUsage( std::cout, usage, columns );
        exit( 1 );
    }

    // Setup
    // =====
    //
    bool verbose = false, quiet = false, force = false;
    bool dxt1_quality = false;
    unsigned int ix = 1024, iy = 1024;
    unsigned int tileRes = 32;
    if( options[ VERBOSE   ] ) verbose = true;
    if( options[ QUIET     ] ) quiet = true;
    if( options[ FORCE     ] ) force = true;

    if( options[ DXT1_QUALITY ] ) dxt1_quality = true;
    if( options[ IMAGESIZE ] ) valxval( options[ IMAGESIZE ].arg, ix, iy );

    SMTool::verbose = verbose;
    SMTool::quiet = quiet;

    // Import the filenames into the tilecache
    TileCache tileCache( verbose, quiet );
    for( int i = 0; i < parse.nonOptionsCount(); ++i )
        tileCache.push_back( parse.nonOption( i ) );

    // load up the tilemap
    ImageBuf *tilemap = NULL;
    if( options[ RECONSTRUCT ] ){
        tilemap = SMTool::openTilemap( options[ RECONSTRUCT ].arg );
        if(! tilemap ){
            if(! quiet ){
                cout << "ERROR.SMTool: unable to load tilemap." << endl;
            }
            exit(1);
        }
        SMF *smf = NULL;
        if( (smf = SMF::open( options[ RECONSTRUCT ].arg )) ){
            tileCache.push_back( options[ RECONSTRUCT ].arg );
            delete smf;
        }
    }

    if( tileCache.getNTiles() == 0 ){
        if(! quiet ){
            cout << "ERROR.TileCache: no files specified for processing." << endl;
        }
        exit(1);
    }
    else if( tileCache.getNTiles() == 1 ){
        if( verbose ){
            cout << "INFO.TileCache: Only one file, assuming large map provided" << endl;
        }
        tileCache.setTileRes( 32 );
    }
    else {
        if( verbose ){
            cout << "INFO.TileCache:" << endl;
            cout << "\tFiles: " << tileCache.getNFiles() << endl;
            cout << "\tTiles: " << tileCache.getNTiles() << endl;
            cout << "\ttileRes: " << tileCache.getTileRes() << endl;
        }
    }
    tileRes = tileCache.getTileRes();

    //filter list
    vector<unsigned int> filter;
    if( options[ FILTER ] ){
        filter = expandString( options[ FILTER ].arg );
    }
    else {
        // have to generate a vector with consecutive numbering;
        filter.resize( tileCache.getNTiles() );
        for(unsigned int i = 0; i < filter.size(); ++i) filter[i] = i;
    }

    // output creation
    SMT *smt = NULL;
    if( options[ OUTPUT ] ){
        string fileName = options[ OUTPUT ].arg;
        if( (smt = SMT::open( fileName, verbose, quiet, dxt1_quality )) ){
            if( verbose ) cout << "INFO.smt: opened " << fileName << endl;
            tileRes = smt->getTileRes();
            tileCache.setTileRes( tileRes );
        }
        else if( (smt = SMT::create( fileName, force, verbose, quiet, dxt1_quality )) ){
            if( verbose ) cout << "INFO.smt: created " << fileName << endl;
            smt->setTileRes( tileRes );
        }
        else {
            if(! quiet ) cout << "ERROR.smt: unable to create " << fileName << endl;
            exit(1);
        }
    }

    // tileRes Override.
    // TileRes is set to be the size of the first tile loaded.
    // The existing output SMT overrides the tileRes value
    // Finally specifying it on the command line overrides the rest.
    // WARNING, if your output file has a different tileRes then all the tiles will be deleted.
    if( options[ TILERES ] ){
        tileRes = stoi( options[ TILERES ].arg );
        smt->setTileRes( tileRes );
        tileCache.setTileRes( tileRes );
    }

    // MapSize & Stride
    unsigned int mx = 2, my = 2;
    unsigned int hstride = 0, vstride = 0;
    if( options[ MAPSIZE ] ){
        valxval( options[ MAPSIZE ].arg, mx, my);
        hstride = mx * 512 / tileRes;
        vstride = my * 512 / tileRes;
    }
    else if( options[ STRIDE ] ){
        hstride = stoi( options[ STRIDE ].arg );
    }

    // Setup
    ImageBuf *buf = NULL;
    ImageSpec spec;
    ImageBuf fix;
    ROI iroi( 0, ix, 0, iy, 0, 1, 0, 4 );
    ROI mroi( 0, mx * 512, 0, my * 512, 0, 1, 0, 4 );

    // Process tiles Seperately
    if( options[ SEPARATE ] ){
        for( auto i = filter.begin(); i != filter.end(); ++i ){
            buf = tileCache.getTile( *i );
            if( options[ OUTPUT ] ){
                smt->append( buf );
            }
            else {
                char name[32];
                sprintf( name, "tile_%07i.tif", *i);
                buf->save( name, "tif");
            }
            delete buf;
        }
        exit(0);
    }

    // reconstruct / collate Tiles
    string imagename;
    if( options[ RECONSTRUCT ] ){
        buf = SMTool::reconstruct( tileCache, tilemap );
        imagename = "reconstruct.tif";
    }
    else if( options[ COLLATE ] || tileCache.getNTiles() == 1 ){
        if( tileCache.getNTiles() == 1) buf = tileCache.getOriginal( 0 );
        else buf = SMTool::collate( tileCache, hstride, vstride );
        imagename = "collate.tif";
    }
    if( buf ) fix.copy( *buf );
    else exit( 1 );

    if( options[ OUTPUT ] ){
        cout << "INFO: Scaling to " << mroi.xend << "x" << mroi.yend << endl;
        buf->clear();
        ImageBufAlgo::resample( *buf, fix, false, mroi );
        // process image into tiles
        SMTool::imageToSMT( smt, buf );
    }
    else {
        if( options[ IMAGESIZE ] ){
            cout << "INFO: Scaling to " << iroi.xend << "x" << iroi.yend << endl;
            buf->clear();
            ImageBufAlgo::resample( *buf, fix, false, iroi );
        }
        buf->save( imagename, "tif" );
        if( verbose ) cout << "INFO: Image saved as " << imagename << endl;
    }


    delete[] options;
    delete[] buffer;
    exit( 0 );
}
