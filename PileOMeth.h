#include <inttypes.h>
#include <stdio.h>
#include <zlib.h>
#include "htslib/sam.h"
#include "htslib/faidx.h"

/*! @typedef
 @abstract Structure to hold one region defined in a BED file
 @field	tid	The chromosome ID, defined by bam_hdr_t
 @field start	0-based start position
 @field end	1-based end position
 @field	strand	0: Ignore strand (".")
                1: Top strand ("+")
                2: Bottom strand ("-")

 @discussion The start and end coordinates will be truncated if they go beyond
 the bounds of a chromosome, as indicated by bam_hdr_t.
*/
typedef struct {
    int32_t tid, start, end; //0-based, [start, end)
    int8_t strand; //0: ., 1: +, 2: -
} bedRegion;

/*! @typedef
 @abstract Structure to hold one region defined in a BED file
 @field region	Pointer to the regions
 @field	n	Number of regions
 @field	m	maxmum number of regions that can currently be stored.

 @discussion You must free this with destroyBED()
*/
typedef struct {
    bedRegion *region;
    int32_t n, m; //Current (n) and maximal possible (m) number of regions
} bedRegions;

/*! @typedef
 @abstract Global configuration information structure
 @field	keepCpG	1: Output CpG metrics
 @field keepCpH 1: Output CpH metriks 
 @field keepCHG	1: Output CHG metrics
 @field	keepCHH	1: Output CHH metrics
 @field	minMapq	Minimum MAPQ value to include a read (-q)
 @field	minPhred	Minimum Phred score to include a base (-p)
 @field	keepDupes	1: Include marked duplicates when calculating metrics
 @field	maxDepth	Maximum depth for the pileup
 @field	minDepth	Minimum depth for outputing methylation metrics
 @field keepDiscordant	0: Do not include discordantly aligned reads when calculating metrics
 @field	keepSingleton	0: Do not include singletons when calculating metrics
 @field merge   1: Merge Cs in either a CpG or CHG context into single entries
 @field methylKit       Output in a format compatible with methylKit
 @field output_fp	Output file pointers (to CpG, CHG, and CHH, respectively)
 @field	reg	A region specified by -r
 @field fp	Input file pointer (must be a BAM or CRAM file)
 @field	bai	The index for fp
 @field bed	Pointer to regions specified in a BED file (-l option)
 @field fai	Fasta file index pointer
*/
typedef struct {
    int keepCpG, keepCpH, keepCHG, keepCHH;
    int minMapq, minPhred, keepDupes, maxDepth, minDepth;
    int keepDiscordant, keepSingleton;
    int merge, methylKit;
    int fraction, counts, logit;
    FILE **output_fp;
    char *reg;
    htsFile *fp;
    hts_idx_t *bai;
    char *bedName;
    bedRegions *bed;
    faidx_t *fai;
    int bounds[16];
} Config;

/*! @typedef
 @abstract Structure given to the pileup filtering function
 @field	config:	The Config* structure containing the settings
 @field hdr:	The input header
 @field iter:	The alignment iterator that should be traversed.
*/
typedef struct {
    Config *config;
    bam_hdr_t *hdr;
    hts_itr_t *iter;
} mplp_data;

/*! @function
 @abstract Determine the strand from which a read arose
 @param	b	Pointer to an alignment
 @returns	1, original top; 2, original bottom; 3, complementary to the original top; 4, complementary to the original bottom
 @discussion There are two methods used to determine the strand of origin. The 
 first method uses XG auxiliary tags as output by Bison and Bismark. For details
 on how strand is determined from this, see the source code for this function or
 the documentation for either Bison or bismark. This method can handle non-
 directional libraries. If an XG auxiliary tag is not present, then the
 orientation of each alignment (and whethers it's read #1 or #2 of a pair, if
 applicable) determines the strand.
*/
int getStrand(bam1_t *b);

/*! @typedef
 @abstract	Positional methylation metrics for a single strand
 @field l	Current length
 @field m	Maximum length
 @field unmeth	Number of unmethylated observations for each position.
 @field meth	Number of methylated observations for each position.
*/
typedef struct {
    int32_t l, m;
    uint32_t *unmeth1, *unmeth2;
    uint32_t *meth1, *meth2;
} strandMeth;

//bed.c
int posOverlapsBED(int32_t tid, int32_t pos, bedRegions *regions, int idxBED);
int spanOverlapsBED(int32_t tid, int32_t start, int32_t end, bedRegions *regions, int *idx);
int readStrandOverlapsBED(bam1_t *b, bedRegion region);
void sortBED(bedRegions *regions);
void destroyBED(bedRegions *regions);
bedRegions *parseBED(char *fn, bam_hdr_t *hdr);

//pileup.c
int cust_mplp_auto(bam_mplp_t iter, int *_tid, int *_pos, int *n_plp, const bam_pileup1_t **plp);

//svg.c
/*! @function
 @abstract Create the actual SVG files that the user can view
 @param opref	The output filename prefix (files will be opref_OT.svg and so on).
 @param meths	The struct holding the methylation metrics for each of the 4 strands. If a strand is not present, it's length (->l) should be 0
 @param which   Denotes which types of Cytosines were used to generate the methylation metric. Bit 0: CpG, bit 1: CHG, bit 2: CHH, bit 3: CpH (these can be combined)
*/
void makeSVGs(char *opref, strandMeth **meths, int which);

/*! @function
 @abstract Print tab-separated methylation metrics to the command line. These can be manually analyzed.
 @param meths	The struct holding the methylation metrics for each of the 4 strands. If a strand is not present, it's length (->l) should be 0
*/
void makeTXT(strandMeth **meths);

//common.c
/*! @function
 @abstract Return 1 if the base is part of a CpG/CHG/CHH/CpH
 @param seq	The genomic sequence
 @param pos	The index within the sequence
 @param seqlen	The length of the sequence
*/
int isCpG(char *seq, int pos, int seqlen);
int isCHG(char *seq, int pos, int seqlen);
int isCHH(char *seq, int pos, int seqlen);
int isCpH(char *seq, int pos, int seqlen);
/*! @function
 @abstract Determine what strand an alignment originated from
 @param b	The alignment in question.
 @returns	1: original top, 2: original bottom, 3: complementary to
		original top, 4: complementary to original bottom
 @discussion	This function will optionally use the XR and XG auxiliary tags
		if they're present. Without them, non-directional libraries
		(those producing CTOT and CTOB alignments) can't be processed,
		since they're otherwise indistinguishable from OT and OB
		alignments.
*/
int getStrand(bam1_t *b);

/*! @function
 @abstract The filter function used by all of the internal pileup methods
 @param data	An mplp_data struct
 @param b	An alignment
 @discussion	This is actually described in the HTSlib documentation
*/
int filter_func(void *data, bam1_t *b);

//Used internally by the pileup-based functions
int updateMetrics(Config *config, const bam_pileup1_t *plp);
