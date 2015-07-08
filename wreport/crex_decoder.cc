#include "bulletin.h"
#include "bulletin/buffers.h"
#include "bulletin/internals.h"
#include <cstring>
#include "config.h"

// #define TRACE_DECODER

#ifdef TRACE_DECODER
#define TRACE(...) fprintf(stderr, __VA_ARGS__)
#define IFTRACE if (1)
#else
#define TRACE(...) do { } while (0)
#define IFTRACE if (0)
#endif

using namespace std;

namespace wreport {
namespace bulletin {
namespace {

void decode_header(bulletin::CrexInput& in, CrexBulletin& out)
{
    /* Read crex section 0 (Indicator section) */
    in.check_available_data(6, "initial header of CREX message");
    if (strncmp((const char*)in.cur, "CREX++", 6) != 0)
        in.parse_error("data does not start with CREX header (\"%.6s\" was read instead)", in.cur);

    in.skip_data_and_spaces(6);
    TRACE(" -> is CREX\n");

    /* Read crex section 1 (Data description section) */
    in.mark_section_start(1);

    /* T<version> */
    if (*in.cur != 'T')
        in.parse_error("version not found in CREX data description");

    {
        char edition[11];
        in.read_word(edition, 11);
        if (sscanf(edition, "T%02d%02d%02d",
                    &(out.master_table_number),
                    &(out.edition),
                    &(out.table)) != 3)
            error_consistency::throwf("Edition (%s) is not in format Ttteevv", edition);
        TRACE(" -> edition %d\n", strtol(edition + 1, 0, 10));
    }

    /* A<atable code> */
    in.check_eof("A code");
    if (*in.cur != 'A')
        in.parse_error("A Table informations not found in CREX data description");
    {
        char atable[20];
        in.read_word(atable, 20);
        TRACE("ATABLE \"%s\"\n", atable);
        int val = strtol(atable+1, 0, 10);
        switch (strlen(atable)-1)
        {
            case 3:
                out.type = val;
                out.subtype = 255;
                out.localsubtype = 0;
                TRACE(" -> category %d\n", strtol(atable, 0, 10));
                break;
            case 6:
                out.type = val / 1000;
                out.subtype = 255;
                out.localsubtype = val % 1000;
                TRACE(" -> category %d, subcategory %d\n", val / 1000, val % 1000);
                break;
            default:
                error_consistency::throwf("Cannot parse an A table indicator %zd digits long", strlen(atable));
        }
    }

    /* data descriptors followed by (E?)\+\+ */
    in.check_eof("data descriptor section");

    out.has_check_digit = false;
    while (1)
    {
        if (*in.cur == 'B' || *in.cur == 'R' || *in.cur == 'C' || *in.cur == 'D')
        {
            in.check_available_data(6, "one data descriptor");
            out.datadesc.push_back(descriptor_code(in.cur));
            in.skip_data_and_spaces(6);
        }
        else if (*in.cur == 'E')
        {
            out.has_check_digit = true;
            in.has_check_digit = true;
            in.expected_check_digit = 1;
            in.skip_data_and_spaces(1);
        }
        else if (*in.cur == '+')
        {
            in.check_available_data(1, "end of data descriptor section");
            if (*(in.cur+1) != '+')
                in.parse_error("data descriptor section ends with only one '+'");
            in.skip_data_and_spaces(2);
            break;
        }
    }
    IFTRACE{
        TRACE(" -> data descriptor section:");
        for (vector<Varcode>::const_iterator i = out.datadesc.begin();
                i != out.datadesc.end(); ++i)
            TRACE(" %01d%02d%03d", WR_VAR_F(*i), WR_VAR_X(*i), WR_VAR_Y(*i));
        TRACE("\n");
    }

    // Load tables and set category/subcategory
    out.load_tables();
}


struct CrexParser : public bulletin::UncompressedDecoder
{
    CrexInput& in;

    CrexParser(Bulletin& bulletin, unsigned subset_idx, CrexInput& in)
        : bulletin::UncompressedDecoder(bulletin, subset_idx), in(in)
    {
    }

    const Var& read_variable(Varinfo info)
    {
        // Create the new Var
        Var var(info);

        // Parse value from the data section
        const char* d_start;
        const char* d_end;
        in.parse_value(info->len, info->type != Vartype::String, &d_start, &d_end);

        /* If the variable is not missing, set its value */
        if (*d_start != '/')
        {
            if (info->type == Vartype::String)
            {
                const int len = d_end - d_start;
                string buf(d_start, len);
                var.setc(buf.c_str());
            } else {
                int val = strtol((const char*)d_start, 0, 10);
                var.seti(val);
            }
        }

        /* Store the variable that we found */
        output_subset.store_variable(var);
        IFTRACE{
            TRACE("define_variable: stored variable: "); var.print(stderr); TRACE("\n");
        }
        return output_subset.back();
    }

    void define_variable(Varinfo info) override
    {
        read_variable(info);
    }

    const Var& define_semantic_variable(Varinfo info) override
    {
        return read_variable(info);
    }
};

void decode_data(bulletin::CrexInput& in, CrexBulletin& out)
{
    /* Decode crex section 2 (data section) */
    in.mark_section_start(2);

    // Scan the various subsections
    for (unsigned i = 0; ; ++i)
    {
        CrexParser parser(out, i, in);
        parser.run();

        in.skip_spaces();
        in.check_eof("end of data section");

        if (*in.cur != '+')
            in.parse_error("there should be a '+' at the end of the data section");
        ++in.cur;

        /* Peek at the next character to see if it's end of section */
        in.check_eof("end of data section");
        if (*in.cur == '+')
        {
            ++in.cur;
            break;
        }
    }
    in.skip_spaces();

    /* Decode crex optional section 3 (optional section) */
    in.mark_section_start(3);
    in.check_available_data(4, "CREX optional section 3 or end of CREX message");
    if (strncmp(in.cur, "SUPP", 4) == 0)
    {
        for (in.cur += 4; strncmp(in.cur, "++", 2) != 0; ++in.cur)
            in.check_available_data(2, "end of CREX optional section 3");
        in.skip_spaces();
    }

    /* Decode crex end section 4 */
    in.mark_section_start(4);
    in.check_available_data(4, "end of CREX message");
    if (strncmp(in.cur, "7777", 4) != 0)
        in.parse_error("unexpected data after data section or optional section 3");
}

}
}

void CrexBulletin::decode_header(const std::string& buf, const char* fname, size_t offset)
{
    clear();
    this->fname = fname;
    this->offset = offset;

    bulletin::CrexInput in(buf, fname, offset);
    bulletin::decode_header(in, *this);
}

void CrexBulletin::decode(const std::string& buf, const char* fname, size_t offset)
{
    clear();
    this->fname = fname;
    this->offset = offset;

    bulletin::CrexInput in(buf, fname, offset);
    bulletin::decode_header(in, *this);
    bulletin::decode_data(in, *this);
}

}
