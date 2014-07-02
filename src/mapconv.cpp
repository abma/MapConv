// local headers
#include "smt.h"
#include "smf.h"
#include "optionparser.h"
#include "util.h"

// Built in headers
#include <cstring>
#include <string>
#include <fstream>


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
    VERBOSE, HELP, QUIET, 
    //File Operations
    IFILE, OVERWRITE,
    // Specification
    MAPSIZE,
    FLOOR, CEILING,
    TILERES,
    // Source materials
    HEIGHT, TYPE, MAP, MINI, METAL, FEATURES, GRASS,
    INVERT,
    // Compression
    SLOW_DXT1, 
    // Deconstruction
    EXTRACT, 
};

const option::Descriptor usage[] = {
    { UNKNOWN, 0, "", "", Arg::None,
        "USAGE: mapconv [options]\n"
        "  eg. mapconv ...\n"
        "\nGENERAL OPTIONS:" },
    { HELP, 0, "h", "help", Arg::None,
        "  -h,  \t--help  \tPrint usage and exit." },
    { VERBOSE, 0, "v", "verbose", Arg::None,
        "  -v,  \t--verbose  \tPrint extra information." },
    { QUIET, 0, "q", "quiet", Arg::None,
        "  -q,  \t--quiet  \tSupress output." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nFILE OPS:" },
    { IFILE, 0, "f", "file", Arg::Required,
        "  -f,  \t--file=mymap.smf  \tFile to operate on, will create if it doesnt exist" },
    { OVERWRITE, 0, "", "overwrite", Arg::Required,
        "\t--overwrite  \tOverwrite existing files" },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nSPECIFICATIONS:" },
    { MAPSIZE, 0, "", "mapsize", Arg::Required,
        "\t--mapsize=XxY  \tWidth and length of map, in spring map units eg. '--mapsize=4x4', must be multiples of two." },
    { FLOOR, 0, "y", "floor", Arg::Numeric,
        "  -y,  \t--floor=1.0f  \tMinimum height of the map." },
    { CEILING, 0, "Y", "ceiling", Arg::Numeric,
        "  -Y,  \t--ceiling=1.0f  \tMaximum height of the map." },
    { TILERES, 0, "", "tileres=32", Arg::Numeric,
        "\t--tileres=X  \tXY resolution of tiles referenced, eg. '--tileres=32'." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nCREATION:" },
    { HEIGHT, 0, "", "height", Arg::Required,
        "\t--height=height.tif  \tImage to use for heightmap." },
    { INVERT, 0, "", "invert", Arg::None,
        "\t--invert \tInvert the heightmap."},
    { TYPE, 0, "", "type", Arg::Required,
        "\t--type=type.tif  \tImage to use for typemap." },
    { MAP, 0, "", "map", Arg::Required,
        "\t--map=map.tif  \tImage to use for tilemap." },
    { MINI, 0, "", "mini", Arg::Required,
        "\t--mini=mini.tif  \tImage to use for minimap." },
    { METAL, 0, "", "metal", Arg::Required,
        "\t--metal=metal.tif  \tImage to use for metalmap." },
    { FEATURES, 0, "", "features", Arg::Required,
        "\t--features=list.csv  \tList of features."},
    { GRASS, 0, "", "grass", Arg::Required,
        "\t--grass=grass.tif  \tImage to use for grassmap." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nCOMPRESSION:" },
    { SLOW_DXT1, 0, "", "slow_dxt1", Arg::None,
        "\t--slow_dxt1  \tUse slower but better analytics when compressing DXT1 textures" },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nDECONSTRUCTION:" },
    { EXTRACT, 0, "e", "extract", Arg::None,
        "  -e,  \t--extract  \tExtract images from loaded SMF." },

    { UNKNOWN, 0, "", "", Arg::None,
        "\nEXAMPLES:\n"
        "  mapconv -x 8 -z 8 -y -10 -Y 256 --height height.tif --metal"
        " metal.png -smts tiles.smt -tilemap tilemap.tif -o mymap.smf\n"
        "  mapconv -i oldmap.smf -o prefix" },
    {0,0,0,0,0,0}
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
        std::cout << "Unknown option: " << std::string( opt->name, opt->namelen ) << "\n";
        fail = true;
    }
    if( fail ) exit( 1 );
    if( parse.error() ) exit( 1 );


    if( options[ HELP ] || argc == 0) {
        int columns = getenv( "COLUMNS" ) ? atoi( getenv( "COLUMNS" ) ) : 80;
        option::printUsage( std::cout, usage, columns );
        exit( 1 );
    }

    bool verbose = false, quiet = false, overwrite = false,
         slow_dxt1 = false, invert = false;
    unsigned int mx = 2, my = 2;
    if( options[ VERBOSE   ] ) verbose = true;
    if( options[ QUIET     ] ) quiet = true;
    if( options[ SLOW_DXT1 ] ) slow_dxt1 = true;
    if( options[ INVERT    ] ) invert = true;
    if( options[ OVERWRITE ] ) overwrite = true;

    // output creation
    SMF *smf = NULL;
    if( options[ IFILE ] ){
        string filename = options[ IFILE ].arg;
        smf = SMF::open( filename, verbose, quiet );
        if(! smf ) smf = SMF::create( filename, overwrite, verbose, quiet );
        if(! smf ){
            if(! quiet )cout << "error.smf: unable to create " << filename << endl;
            exit(1);
        }
    }
    
    vector<string> smts;    
    for( int i = 0; i < parse.nonOptionsCount(); ++i ){
        smts.push_back( parse.nonOption( i ) );

    }
    smf->addTileFiles( smts );

    if( options[ MAPSIZE ] ){
        valxval( options[ MAPSIZE ].arg, mx, my );
        smf->setSize( mx, my );
    }

    if( options[ HEIGHT ] ){
        ImageBuf heightBuf( options[ HEIGHT ].arg );
        smf->writeHeight( &heightBuf );
    }
    if( options[ TYPE ] ){
        ImageBuf typeBuf( options[ TYPE ].arg );
        smf->writeType( &typeBuf );
    }
    if( options[ MAP ] ){
        ImageBuf mapBuf( options[ MAP ].arg );
        smf->writeMap( &mapBuf );
    }
    if( options[ MINI ] ){
        ImageBuf miniBuf( options[ MINI ].arg );
        smf->writeMini( &miniBuf );
    }
    if( options[ METAL ] ){
        ImageBuf metalBuf( options[ METAL ].arg );
        smf->writeMetal( &metalBuf );
    }

    ImageBuf *buf = NULL;
    if( options[ EXTRACT ] ){
        if( verbose ) cout << "INFO: Extracting height image" << endl;
        buf = smf->getHeight();
        buf->write("height.tif", "tif" );
        if( verbose ) cout << "INFO: Extracting type image" << endl;
        buf = smf->getType();
        buf->write("type.tif", "tif");
        if( verbose ) cout << "INFO: Extracting map image" << endl;
        buf = smf->getMap();
        buf->write("map.exr", "exr");
        if( verbose ) cout << "INFO: Extracting mini image" << endl;
        buf = smf->getMini();
        buf->write("mini.tif", "tif");
        if( verbose ) cout << "INFO: Extracting metal image" << endl;
        buf = smf->getMetal();
        buf->write("metal.tif", "tif");
        buf = smf->getGrass();
        if( buf ){
            if( verbose ) cout << "INFO: Extracting grass image" << endl;
            buf->write("grass.tif", "tif");
        }
    }

    cout << smf->info();
    return 0;
}
