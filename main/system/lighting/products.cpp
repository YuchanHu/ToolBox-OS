#include "protocol.h"
#include <cctype>
#include <iterator>
#include <string>
namespace toolbox::lighting {
namespace {
// Wire product identifiers and capability facts from APK v1.0.20.
constexpr Product table[] = {
    {Brand::Sidus, "00005", "LS 300d II", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00045", "LS 300x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00055", "LS 600d Pro", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00075", "LS 60d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00085", "LS 60x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "000C5", "LS 600d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "000D5", "LS 600X Pro", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "000E5", "LS 1200d Pro", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "000F5", "MT Pro", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000G5", "LS 600c Pro", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000J5", "Electro Storm XT26", {2700, 6500, -5, 5, true, false, false, true}},
    {Brand::Sidus, "000K5", "Electro Storm CS15", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000L5", "LS 600c Pro II", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000M5", "STORM 1200x", {2500, 10000, -10, 10, true, false, false, true}},
    {Brand::Sidus, "000N5", "STORM 80c", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000P5", "STORM 1000c", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "000Q5", "STORM 400x", {2500, 10000, -10, 10, true, false, false, true}},
    {Brand::Sidus, "000R5", "STORM XT52", {2500, 10000, -10, 10, true, false, false, true}},
    {Brand::Sidus, "000S5", "STORM 700x", {2500, 10000, -10, 10, true, false, false, true}},
    {Brand::Sidus, "000T5", "STORM CS32", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "02005", "Nova P300c", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "02035", "Nova P600c", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "02065", "NOVA II 2x1", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "02085", "NOVA II 1x1", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "04005", "Accent B7c", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "05005", "MC", {3200, 6500, 0, 0, true, true, false, true}},
    {Brand::Sidus, "05006", "MC", {3200, 6500, 0, 0, true, true, false, true}},
    {Brand::Sidus, "05007", "MC", {3200, 6500, 0, 0, true, true, false, true}},
    {Brand::Sidus, "05008", "MC", {3200, 6500, 0, 0, true, true, false, true}},
    {Brand::Sidus, "05010", "MC Pro", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "07006", "INFINIBAR PB3", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "07007", "INFINIBAR PB6", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "07008", "INFINIBAR PB12", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "09005", "INFINIMAT 4", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "09015", "INFINIMAT 16", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "0A005", "VEGA", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "0A015", "VEGA L", {1800, 20000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "20005", "SGC H60", {2700, 6000, 0, 0, true, false, false, true}},
    {Brand::Sidus, "20015", "SGC H120", {2700, 6000, 0, 0, true, false, false, true}},
    {Brand::Sidus, "20025", "SGC P60", {2700, 6500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "20035", "SGC P120", {2700, 6500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40005", "amaran P60x", {3200, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40015", "amaran P60c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40025", "amaran 100d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40035", "amaran 100x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40045", "amaran 200d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40055", "amaran 200x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40065", "amaran COB 60d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40075", "amaran COB 60x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40085", "amaran T2c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40095", "amaran T4c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400B5", "amaran F21x", {2500, 7500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400C5", "amaran F21c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400D5", "amaran F22x", {2500, 7500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400E5", "amaran F22c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400F5", "amaran SM5c", {3200, 6500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400G5", "amaran PT1c", {2700, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400H5", "amaran PT2c", {2700, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400I5", "amaran PT4c", {2700, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400J5", "amaran 150c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400K5", "amaran 300c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400L5", "amaran 60d S", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400M5", "amaran 60x S", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400N5", "amaran 100d S", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400O5", "amaran 100x S", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400P5", "amaran 200d S", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400Q5", "amaran 200x S", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400R5", "amaran 200c", {2500, 7500, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400S5", "amaran P40x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400T5", "amaran Ace 25x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400U5", "amaran Ace 25c", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400V5", "amaran Go", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400W5", "amaran Pano 60c", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400X5", "amaran Pano 120c", {2000, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "400Y5", "amaran Verge", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "400Z5", "amaran Verge Max", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "40145", "amaran Ray 60c", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40165", "amaran Ray 120c", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40185", "amaran Ray 360c", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "40195", "amaran 300x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401A5", "amaran Ray 660c", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "401B5", "amaran Halo 60x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401C5", "amaran Halo 100x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401D5", "amaran Halo 200x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401E5", "amaran Halo 300x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401F5", "amaran Halo 600x", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401R5", "amaran Frame", {2300, 10000, -10, 10, true, true, false, true}},
    {Brand::Sidus, "401S5", "amaran Halo 300x DMX", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "401T5", "amaran Halo 600x DMX", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Sidus, "000B5", "LS 350d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00065", "LS 1500d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Sidus, "00015", "LS 50d", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Godox, "0001", "TL60", {2700, 6500, 0, 0, true, true, false, false}},
    {Brand::Godox, "0002", "TL30", {2700, 6500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0003", "SZ150R", {2800, 6500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0004", "LD75R", {2500, 8500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0005", "LD150R", {2500, 8500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0006", "LD150RS", {2500, 8500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0007", "CL10", {0, 0, 0, 0, false, true, false, false}},
    {Brand::Godox, "0008", "SZ200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0009", "ES45", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "000A", "M600D", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Godox, "000B", "UL60", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "000C", "WL4B", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "000D", "S60", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "000E", "SL100D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "000F", "SL100Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0010", "ML60Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0011", "WL8P", {2700, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0012", "ML30Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0013", "ML30", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0014", "M300D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0015", "S60Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0016", "UL60Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0017", "C5R", {2500, 8500, 0, 0, true, true, false, false}},
    {Brand::Godox, "0018", "TL120", {2700, 6500, 0, 0, true, true, false, false}},
    {Brand::Godox, "0019", "TL180", {2700, 6500, 0, 0, true, true, false, false}},
    {Brand::Godox, "001A", "M200D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "001B", "UL150II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "001C", "M200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "001D", "UL150IIBI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "001E", "M300Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "001F", "ES30", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0020", "M600Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0021", "LA150D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0022", "LA150Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0023", "LA200D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0024", "LA200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0025", "RF1", {2700, 6500, 0, 0, false, false, false, false}},
    {Brand::Godox, "0026", "LED6R", {3200, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0027", "LED6Bi", {3200, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "002A", "TP2R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "002B", "TP4R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "002C", "G1200D", {5600, 5600, 0, 0, true, false, false, true}},
    {Brand::Godox, "002D", "M400R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "002E", "WT40D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "002F", "WT40R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0030", "WT25R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0031", "WT25D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0032", "WT60R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0033", "WT60D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0034", "SL300II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0035", "SL300IIBi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0036", "VL300II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0037", "VL200II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0038", "VL150II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0039", "SL60IID", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "003A", "SL60IIBi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "003B", "SL150III", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "003C", "SL200III", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "003D", "SL300III", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "003E", "SL150IIIbi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "003F", "SL200IIIBi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0040", "SL300IIIbi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0041", "LDX50Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0042", "LDX100Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0043", "LDX50R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0044", "LDX100R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0045", "TP8R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0046", "C7R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0047", "C10R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0048", "FH50Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0049", "FH50R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "004A", "SZ300R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "004B", "MG1200Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "004D", "P300R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "004E", "P600R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "004F", "P2400R", {2800, 5600, 0, 0, true, true, false, true}},
    {Brand::Godox, "0050", "MG800R", {2800, 5600, 0, 0, true, true, false, true}},
    {Brand::Godox, "0053", "F200Bi", {2700, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0054", "F400Bi", {2700, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0055", "F600Bi", {2700, 8500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0056", "P600Bi Hard", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0057", "AT200Bi", {2700, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0058", "TPC2R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0059", "TPC4R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "005A", "TPC8R", {2000, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "005B", "P200Bi", {3200, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "005C", "SL150R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "005D", "SL300R", {2500, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "005E", "LC500 mini", {2500, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "005F", "LC500R mini", {2500, 8500, 0, 0, true, true, false, false}},
    {Brand::Godox, "0060", "M600R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0061", "F7-120Bi", {3200, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0062", "F7-120D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0063", "LC1000", {2500, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0064", "LC1000R", {2500, 8500, -50, 50, true, true, false, false}},
    {Brand::Godox, "0065", "ML60IIBi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0066", "ML100Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0067", "M1000Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0068", "MG2400Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0069", "P600R Hard", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "004C", "FFFF弃用", {2700, 6500, 0, 0, false, false, false, false}},
    {Brand::Godox, "006A", "FR200Bi", {2500, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "006B", "P120Bi", {3200, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "006C", "P120D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "006D", "P1200R Hard", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "006E", "FR150Bi", {2500, 8500, 0, 0, true, false, false, false}},
    {Brand::Godox, "006F", "DL150D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0070", "DL150Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0071", "DL200D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "0072", "DL200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0073", "MG1200R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0074", "LA300R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0075", "LA150R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0076", "P260CPro", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0077", "F800R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0078", "MS60R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0079", "LA300Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "007A", "LP400Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "007B", "LP600Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "007C", "LP1200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "007D", "LP400R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "007E", "LP600R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "007F", "LP1200R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "0080", "M600Bi Pro", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0081", "F400R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0082", "F200R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0083", "F100R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0084", "MS60Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0085", "LA200R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0086", "F200SR", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0088", "MG2400R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0089", "MA5R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "008A", "RS60R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "008B", "RS60Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "008C", "M300R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "008D", "P1200R Hard Pro", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "008E", "ML60R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "008F", "ML100R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0090", "ML150 RF", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0091", "MG4800R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0092", "MG4800D", {5600, 5600, -100, 100, true, false, false, true}},
    {Brand::Godox, "0087", "M1000R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0093", "MG2400Bi SE", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0094", "LA600R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "0095", "LA600Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0096", "FL-D200", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0097", "ML80Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "0098", "S60Bi-CB", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "0099", "P600R Hard Pro", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "009A", "AM1600R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "009B", "LC500 Air", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "009C", "LC500R Air", {2800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "009D", "AT20 Battery", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "009E", "AT20C Battery", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "009F", "FL15Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00A0", "MG6K", {2800, 10000, -100, 100, true, false, false, true}},
    {Brand::Godox, "00A1", "C30R", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "00A2", "F7-300R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00A3", "SL300R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00A4", "ML100Bi(新版）", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00A5", "ML150Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00A6", "ML40Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00A7", "ML40R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "00A8", "LP800Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00A9", "MA5R Plus", {1800, 10000, -50, 50, true, true, false, false}},
    {Brand::Godox, "00AA", "P1200R Hard P8", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00AB", "LR150 Air", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00AC", "LR120 Air", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00AD", "LE300Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00AE", "LE200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00AF", "LE150Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00B0", "LA300D II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "00B1", "LA200D II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "00B2", "LA150D II", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "00B3", "RS100BI", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "00B4", "RS100R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00B5", "SL300 RF", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00B6", "SL200 RF", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00B7", "SL150 RF", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00B8", "LE600Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00B9", "P600R Hard P4", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00BA", "LC500 Air Hard", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00BB", "MG4K", {2800, 10000, -100, 100, true, false, false, true}},
    {Brand::Godox, "00BC", "OP120R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00BD", "OP200R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00BE", "DL625Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00BF", "DL330Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00C0", "DL200Bi", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00C1", "PL600 RF", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00C2", "MG4KR", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00C3", "PL1200 RF", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00C4", "MS15R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00C5", "LE300R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "00C6", "LE200R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "00C7", "LE600R", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "00C8", "ParTrix", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "0051", "RGB01", {2500, 8500, 0, 0, true, true, false, false}},
    {Brand::Godox, "00CA", "LF40R", {2800, 6500, 0, 0, true, true, false, false}},
    {Brand::Godox, "00CB", "UP-C300", {1800, 10000, -100, 100, true, true, false, false}},
    {Brand::Godox, "00CC", "OP120Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "00CD", "OP200Bi", {2800, 6500, 0, 0, true, false, false, true}},
    {Brand::Godox, "00CE", "UP-C150", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00CF", "LT1", {0, 0, 0, 0, false, false, false, false}},
    {Brand::Godox, "00D0", "SL300Bi", {1800, 10000, -100, 100, true, false, false, false}},
    {Brand::Godox, "00D1", "SL200Bi", {1800, 10000, -100, 100, true, false, false, false}},
    {Brand::Godox, "00D2", "LE200D", {5600, 5600, 0, 0, true, false, false, false}},
    {Brand::Godox, "00D3", "B5R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00D4", "LF30BI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00D6", "FL200Bi 22K", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00D7", "AM800R", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00D8", "LF20BI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00D9", "FL100BI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00DA", "MG6KR", {2500, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00DB", "HybridLite P02", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00DC", "LC500R II", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00DD", "LC500R Mini II", {1800, 10000, -100, 100, true, true, false, true}},
    {Brand::Godox, "00DE", "FL600BI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Godox, "00DF", "FL400BI", {2800, 6500, 0, 0, true, false, false, false}},
    {Brand::Lumin, "1", "C4", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "3", "60C", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "5", "60DN", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "7", "220DN", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "8", "320DN", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "10", "80BN", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "12", "80C", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "13", "130DNA", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "14", "200DNA", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "15", "80C_1", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "16", "80BN_1", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "17", "60BNA", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "18", "130BNA", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "19", "200BNA", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "21", "Lx200BNA", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "24", "TL1-7C30", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "25", "Lx200DNA", {5600, 5600, 0, 0, false, false, false, true}},
    {Brand::Lumin, "27", "Lx200N", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "28", "x200BNA", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "29", "x200N", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "30", "60CA", {2700, 10000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "31", "400BNS", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "32", "300C", {1800, 20000, -100, 100, true, true, false, true}},
    {Brand::Lumin, "35", "800BNS", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "36", "F200B", {2700, 6500, 0, 0, true, false, false, true}},
    {Brand::Lumin, "37", "130CA", {1800, 20000, -100, 100, true, true, false, true}},
    {Brand::Jinbei, "1", "EFII-100", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "2", "EFII-150", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "3", "EFII-200", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "4", "EFII-60", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "5", "EF-300", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "6", "EFP-50", {2700, 7500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "7", "EF-200BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "8", "EFP-400", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "9", "EFD-150", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "10", "EFD-200", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "11", "EFP-100", {2700, 7500, -10, 10, true, false, false, false, false}},
    {Brand::Jinbei, "12", "EFD-300", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "13", "EFD-500", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "14", "EFD-60M", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "15", "EFD-60BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "17", "EFT-360", {2000, 10000, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "18", "EFT-550", {2700, 10000, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "19", "EF-P10", {2000, 10000, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "20", "ICE-LIGHT3", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "21", "EFT-8C", {2000, 10000, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "22", "EFT-8C BI", {2700, 10000, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "23", "LX-100", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "24", "LX-100BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "25", "LX-60", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "26", "LX-60BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "27", "EFD-300BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "28", "EF-300BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "29", "EFC-150", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "30", "EF-60RGB", {2000, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "31", "EFP-100RGB", {2000, 10000, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "33", "Soluna 200Bi", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "34", "Soluna 500pro", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "39", "LED 1500D", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "40", "LED 2000D", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "41", "LED 1000", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "42", "LED 200D", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "43", "LED 160D", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "49", "EFIII-100", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "50", "EFIII-150", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "51", "EFIII-200", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "52", "EFII-200BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "53", "EF-150BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "54", "EFII-300", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "55", "Soluna 150", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "56", "L60-B", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "57", "EF-600", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "58", "EF-150PRO", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "59", "EF-220", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "60", "EF-160BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "61", "EF-220BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "62", "EFT-361", {2000, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "63", "EFT-551", {2700, 7500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "64", "EF-600BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "65", "EL-300", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "66", "EL-300BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "67", "EF-80", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "68", "EF-80BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "69", "EF-120", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "70", "EF-120BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "71", "EFT-220RGB", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "72", "EFT-860", {2000, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "73", "Soluna L-60", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "74", "Soluna L100", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "76", "Soluna 80", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "77", "Soluna 80Bi", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "78", "Soluna 120", {5500, 5500, 0, 0, false, false, true, false, true}},
    {Brand::Jinbei, "79", "Soluna120Bi", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "80", "EFT_220RGB", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "81", "P6PRO", {2700, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "84", "P80RGB", {2000, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "85", "P80BI", {2700, 7500, -10, 10, true, false, true, false, false}},
    {Brand::Jinbei, "90", "L120-B", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "91", "LED 300BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "92", "LED 120BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "93", "LED 80BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "94", "LED 24RGB", {2000, 7500, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "95", "LED 8RGB", {2000, 10000, -10, 10, true, true, true, false, true}},
    {Brand::Jinbei, "96", "EF-60BI", {2700, 6500, -10, 10, true, false, true, false, true}},
    {Brand::Jinbei, "97", "EF_220", {5500, 5500, 0, 0, false, false, true, false, false}},
    {Brand::Jinbei, "98", "EF_220BI", {2700, 6500, -10, 10, true, false, true, false, false}},
    {Brand::Ulanzi, "0D&01", "VL200Bi", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&02", "VL120C", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Ulanzi, "0D&03", "VL120Bi", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&04", "GL50", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&05", "COB140W", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&06", "EC65", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&07", "COB65W RGB", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Ulanzi, "0D&08", "EKA60W", {2700, 6500, 0, 0, true, false, false, false}},
    {Brand::Ulanzi, "0D&A2", "AL120", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&A3", "AL60", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&A6", "LC60", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&A7", "K6500", {6500, 6500, 0, 0, true, false, false, false}},
    {Brand::Ulanzi, "0D&A9", "LA30", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Ulanzi, "0D&AF", "AL40 RGB", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Ulanzi, "0D&B1", "FOLD8", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Ulanzi, "0D&B2", "VL300Bi", {2700, 6500, 0, 0, true, false, true, false}},
    {Brand::Ulanzi, "0D&B5", "UA15", {2700, 6500, 0, 0, true, true, true, false}},
    {Brand::Zhiyun, "PL103", "MOLUS G60", {2700, 6500, 0, 0, true, false, false, false}},
    {Brand::Zhiyun, "PL105", "MOLUS X100", {2700, 6500, 0, 0, true, false, false, false}},
};
std::string key(std::string_view s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c))
            out += std::toupper(c);
    return out;
}
} // namespace
const Product *products(size_t &count) {
    count = std::size(table);
    return table;
}
const Product *find_product(Brand brand, std::string_view value) {
    auto name = key(value);
    for (auto &p : table)
        if (p.brand == brand && (name == key(p.id) || name == key(p.name)))
            return &p;
    return nullptr;
}
} // namespace toolbox::lighting

namespace toolbox::lighting {
namespace {
struct NanModel {
    const char *id;
    NanliteProfile profile;
};
constexpr NanModel nan_models[] = {
    {"000003", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000005", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000006", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000007", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000008", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000012", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000016", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"00001A", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"00001B", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"00001F", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000021", {{2700, 6500, 0, 0, false, false, false, false}, 1, 1, 0}},
    {"000018", {{2700, 6500, -8, 8, false, false, false, false}, 1, 1, 0}},
    {"000019", {{2700, 6500, -8, 8, false, false, false, false}, 1, 1, 0}},
    {"000001", {{2700, 6500, 0, 0, true, false, false, false}, 1, 1, 0}},
    {"000002", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000013", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000017", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000028", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000029", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00002B", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00002C", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000031", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000032", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000038", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000039", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00003A", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00003B", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00003C", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00003D", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00003E", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"001006", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"002007", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00200C", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00200D", {{2700, 6500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000020", {{2700, 7500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"000022", {{2700, 7500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00002D", {{2700, 7500, 0, 0, true, false, false, false}, 2, 1, 0}},
    {"00002A", {{2700, 6500, -8, 8, true, false, false, false}, 2, 1, 0}},
    {"000030", {{2700, 6500, -8, 8, true, false, false, false}, 2, 1, 0}},
    {"000033", {{2700, 6500, -8, 8, true, false, false, false}, 2, 1, 0}},
    {"001001", {{2700, 6500, -8, 8, true, false, false, false}, 2, 1, 0}},
    {"000009", {{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00000A", {{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000014", {{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000015", {{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00002E", {{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00000B", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00000C", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00000D", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000041", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000042", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000043", {{2700, 12000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00000E", {{2700, 6500, -10, 10, true, true, false, false}, 1, 2, 8}},
    {"00000F", {{2700, 6500, -10, 10, true, true, false, false}, 1, 2, 8}},
    {"000010", {{2700, 20000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000011", {{2700, 20000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00001C", {{1800, 20000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"00004B", {{1800, 20000, -10, 10, true, true, false, false}, 1, 1, 8}},
    {"000026", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000027", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000035", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000036", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000037", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000045", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"000046", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"00004A", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"002003", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"002004", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"002005", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"002006", {{2700, 7500, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"00002F", {{1800, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"001002", {{1000, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"001003", {{1000, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"001004", {{1000, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"001005", {{1000, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"001007", {{1000, 20000, -20, 20, true, true, false, false}, 1, 1, 9}},
    {"002001", {{2700, 12000, -15, 15, true, true, false, false}, 1, 1, 9}},
    {"002002", {{2700, 12000, -15, 15, true, true, false, false}, 1, 1, 9}},
    {"002008", {{2400, 12000, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"00200A", {{2400, 12000, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"00200E", {{2400, 12000, -15, 15, true, true, false, false}, 1, 1, 8}},
    {"002009", {{2400, 12000, -15, 15, true, true, false, false}, 1, 1, 9}},
    {"00003F", {{2700, 12000, -15, 15, true, true, false, false}, 1, 1, 16}},
    {"000040", {{2700, 12000, -15, 15, true, true, false, false}, 1, 1, 16}},
};
} // namespace
const NanliteProfile *nanlite_profile(std::string_view id) {
    for (auto &p : nan_models)
        if (id == p.id)
            return &p.profile;
    return nullptr;
}
} // namespace toolbox::lighting

namespace toolbox::lighting {
std::vector<Effect> effects_for(Brand brand,std::string_view product,const Capability& caps){
 if(!caps.fx)return {};
 if(brand==Brand::Lyd)return {{1,"闪光1"},{2,"闪光2"},{3,"电视"},{4,"追逐1"},{5,"追逐3"},{6,"烛光"},{7,"警灯"},{8,"救护车"},{9,"消防车"},{10,"火焰1"},{11,"火焰2"},{12,"频闪1"},{13,"频闪2"},{14,"频闪3"},{15,"追逐2"},{16,"烟花1"},{17,"烟花2"},{18,"烟花3"},{19,"派对1"},{20,"派对2"},{21,"浪漫"},{22,"音乐"},{23,"波形1"},{24,"波形2"}};
 if(brand==Brand::Yongnuo)return {{1,"闪电"},{2,"电视"},{3,"烛光"},{5,"烟花"},{8,"警灯"},{11,"闪光"},{15,"派对"},{17,"火焰"}};
 if(brand==Brand::Jinbei){
  if(product=="1")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="2")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="3")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="4")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="5")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="6")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="7")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="8")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="9")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="10")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="12")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="13")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="27")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="33")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="34")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="39")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="40")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="42")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="43")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="49")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="50")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="51")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="52")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="53")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="58")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="59")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="60")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="61")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="65")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="66")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="85")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="97")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="98")return {{1,"电视"},{3,"烟花"},{4,"闪电"}};
  if(product=="14")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="15")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="18")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="22")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="23")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="24")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="25")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="26")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="41")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="64")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="67")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="68")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="69")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="73")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="74")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="76")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="77")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="78")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="93")return {{1,"电视"},{4,"闪电"},{12,"闪光"}};
  if(product=="30")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="62")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="71")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="72")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="80")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="81")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="84")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="94")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="95")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="20")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"}};
  if(product=="28")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"}};
  if(product=="54")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"}};
  if(product=="57")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"}};
  if(product=="91")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"}};
  if(product=="17")return {{1,"电视"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="19")return {{1,"电视"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="21")return {{1,"电视"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="31")return {{1,"电视"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{19,"烟花"},{20,"派对"}};
  if(product=="70")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"}};
  if(product=="79")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"}};
  if(product=="96")return {{1,"电视"},{4,"闪电"},{11,"脉冲"},{12,"闪光"}};
  if(product=="29")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{20,"派对"}};
  if(product=="55")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{12,"闪光"},{17,"警灯"},{20,"派对"}};
  if(product=="63")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{11,"脉冲"},{12,"闪光"}};
  if(product=="92")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{11,"脉冲"},{12,"闪光"}};
  if(product=="56")return {{1,"电视"},{3,"烟花"},{4,"闪电"},{10,"火焰"},{11,"脉冲"},{12,"闪光"}};
  if(product=="90")return {{1,"电视"},{4,"闪电"},{10,"火焰"},{11,"脉冲"},{12,"闪光"}};
 }
 if(brand==Brand::Ulanzi){
  if(product=="0D&01")return {{5,"电视"},{6,"烛光"}};
  if(product=="0D&02")return {{5,"电视"},{6,"烛光"},{7,"火焰"},{9,"警灯"}};
  if(product=="0D&03")return {{5,"电视"},{6,"烛光"}};
  if(product=="0D&04")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"},{6,"烛光"},{24,"警灯"},{27,"派对"}};
  if(product=="0D&05")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"}};
  if(product=="0D&06")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"}};
  if(product=="0D&07")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"},{6,"烛光"},{24,"警灯"},{27,"派对"}};
  if(product=="0D&A2")return {{5,"电视"},{6,"烛光"},{25,"闪电"},{29,"脉冲"},{32,"闪光"}};
  if(product=="0D&A3")return {{5,"电视"},{6,"烛光"},{25,"闪电"},{29,"脉冲"},{32,"闪光"}};
  if(product=="0D&A6")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"}};
  if(product=="0D&A9")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"},{7,"火焰"},{24,"警灯"},{27,"派对"}};
  if(product=="0D&AF")return {{5,"电视"},{6,"烛光"},{7,"火焰"},{9,"警灯"}};
  if(product=="0D&B1")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"},{6,"烛光"},{24,"警灯"},{27,"派对"}};
  if(product=="0D&B2")return {{5,"电视"},{6,"烛光"}};
  if(product=="0D&B5")return {{5,"电视"},{25,"闪电"},{26,"烟花"},{29,"脉冲"},{32,"闪光"},{7,"火焰"},{24,"警灯"},{27,"派对"}};
 }
 return {};
}
}
