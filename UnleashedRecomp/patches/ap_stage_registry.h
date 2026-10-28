#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

// Shared stage registry for Archipelago.
// Keep legacy IDs unchanged. Generated from TOUS LES ACTS(1).txt.
struct APRankActLocations
{
    const char* displayName;
    uint32_t actClear;
    uint32_t rankD;
    uint32_t rankC;
    uint32_t rankB;
    uint32_t rankA;
    const char* clearLabel;
    bool isDlc;
};

static const std::unordered_map<std::string, APRankActLocations> apRankActLocations = {
    {"ActD_MykonosAct1", {"Windmill Isle - Day Act 1", 88000381, 88000382, 88000383, 88000384, 88000385, "Act Clear", false}},
    {"ActD_MykonosAct2", {"Windmill Isle - Day Act 2", 88000371, 88000372, 88000373, 88000374, 88000375, "Act Clear", false}},
    {"ActD_SubMykonos_04", {"Windmill Isle - Day Act 1 - 2 (DLC)", 88000386, 88000387, 88000388, 88000389, 88000390, "Act Clear", true}},
    {"ActD_SubMykonos_05", {"Windmill Isle - Day Act 2 - 2 (DLC)", 88000391, 88000392, 88000393, 88000394, 88000395, "Act Clear", true}},
    {"ActN_MykonosEvil", {"Windmill Isle - Night Act 1", 88000376, 88000377, 88000378, 88000379, 88000380, "Act Clear", false}},
    {"ActN_SubMykonos_03", {"Windmill Isle - Night Act 1 - 2", 88000396, 88000397, 88000398, 88000399, 88000400, "Act Clear", false}},
    {"ActD_Africa", {"Savannah Citadel - Day Act 1", 88000401, 88000402, 88000403, 88000404, 88000405, "Act Clear", false}},
    {"BossEggBeetle", {"Egg Beetle", 88000406, 88000407, 88000408, 88000409, 88000410, "Boss Clear", false}},
    {"ActN_AfricaEvil", {"Savannah Citadel - Night Act 1", 88000411, 88000412, 88000413, 88000414, 88000415, "Act Clear", false}},
    {"ActD_EU", {"Rooftop Run - Day Act 1", 88000416, 88000417, 88000418, 88000419, 88000420, "Act Clear", false}},
    {"BossEggRayBird", {"Egg Devil Ray", 88000421, 88000422, 88000423, 88000424, 88000425, "Boss Clear", false}},
    {"ExStageTails1", {"Tornado Mission 1", 88000426, 88000427, 88000428, 88000429, 88000430, "Mission Clear", false}},
    {"ActN_EUEvil", {"Rooftop Run - Night Act 1", 88000431, 88000432, 88000433, 88000434, 88000435, "Act Clear", false}},
    {"ActD_China", {"Dragon Road - Day Act 1", 88000436, 88000437, 88000438, 88000439, 88000440, "Act Clear", false}},
    {"ActD_SubChina_05", {"Dragon Road - Day Act 1 - 2 (DLC)", 88000441, 88000442, 88000443, 88000444, 88000445, "Act Clear", true}},
    {"ActD_SubChina_03", {"Dragon Road - Day Act 2", 88000446, 88000447, 88000448, 88000449, 88000450, "Act Clear", false}},
    {"ActD_SubChina_06", {"Dragon Road - Day Act 2 - 2 (DLC)", 88000451, 88000452, 88000453, 88000454, 88000455, "Act Clear", true}},
    {"ActD_SubChina_04", {"Dragon Road - Day Act 3", 88000456, 88000457, 88000458, 88000459, 88000460, "Act Clear", false}},
    {"ActD_SubChina_01", {"Dragon Road - Day Act 4 (DLC)", 88000461, 88000462, 88000463, 88000464, 88000465, "Act Clear", true}},
    {"ActD_SubChina_02", {"Dragon Road - Day Act 5 (DLC)", 88000466, 88000467, 88000468, 88000469, 88000470, "Act Clear", true}},
    {"ActN_ChinaEvil", {"Dragon Road - Night Act 1", 88000471, 88000472, 88000473, 88000474, 88000475, "Act Clear", false}},
    {"ActN_SubChina_03", {"Dragon Road - Night Act 1 - 2 (DLC)", 88000476, 88000477, 88000478, 88000479, 88000480, "Act Clear", true}},
    {"ActN_SubChina_01", {"Dragon Road - Night Act 2", 88000481, 88000482, 88000483, 88000484, 88000485, "Act Clear", false}},
    {"ActN_SubChina_02", {"Dragon Road - Night Act 3 (DLC)", 88000486, 88000487, 88000488, 88000489, 88000490, "Act Clear", true}},
    {"BossPhoenix", {"Dark Gaia Phoenix", 88000491, 88000492, 88000493, 88000494, 88000495, "Boss Clear", false}},
    {"ActD_Snow", {"Cool Edge - Day Act 1", 88000496, 88000497, 88000498, 88000499, 88000500, "Act Clear", false}},
    {"ActD_SubSnow_04", {"Cool Edge - Day Act 1 - 2 (DLC)", 88000501, 88000502, 88000503, 88000504, 88000505, "Act Clear", true}},
    {"ActD_SubSnow_01", {"Cool Edge - Day Act 2", 88000506, 88000507, 88000508, 88000509, 88000510, "Act Clear", false}},
    {"ActD_SubSnow_05", {"Cool Edge - Day Act 2 - 2 (DLC)", 88000511, 88000512, 88000513, 88000514, 88000515, "Act Clear", true}},
    {"ActD_SubSnow_02", {"Cool Edge - Day Act 3 (DLC)", 88000516, 88000517, 88000518, 88000519, 88000520, "Act Clear", true}},
    {"ActD_SubSnow_03", {"Cool Edge - Day Act 4 (DLC)", 88000521, 88000522, 88000523, 88000524, 88000525, "Act Clear", true}},
    {"ActN_SnowEvil", {"Cool Edge - Night Act 1", 88000526, 88000527, 88000528, 88000529, 88000530, "Act Clear", false}},
    {"ActN_SubSnow_01", {"Cool Edge - Night Act 2 (DLC)", 88000531, 88000532, 88000533, 88000534, 88000535, "Act Clear", true}},
    {"ActN_SubSnow_02", {"Cool Edge - Night Act 3 (DLC)", 88000536, 88000537, 88000538, 88000539, 88000540, "Act Clear", true}},
    {"BossDarkGaiaMoray", {"Dark Moray", 88000541, 88000542, 88000543, 88000544, 88000545, "Boss Clear", false}},
    {"ActD_NY", {"Skyscraper Scamper - Day Act 1", 88000546, 88000547, 88000548, 88000549, 88000550, "Act Clear", false}},
    {"ActD_SubNY_03", {"Skyscraper Scamper - Day Act 1 - 2 (DLC)", 88000551, 88000552, 88000553, 88000554, 88000555, "Act Clear", true}},
    {"ActD_SubNY_02", {"Skyscraper Scamper - Day Act 3 (DLC)", 88000556, 88000557, 88000558, 88000559, 88000560, "Act Clear", true}},
    {"ActN_NYEvil", {"Skyscraper Scamper - Night Act 1", 88000561, 88000562, 88000563, 88000564, 88000565, "Act Clear", false}},
    {"ActN_SubNY_02", {"Skyscraper Scamper - Night Act 2 (DLC)", 88000566, 88000567, 88000568, 88000569, 88000570, "Act Clear", true}},
    {"ActN_SubNY_01", {"Skyscraper Scamper - Night Act 3 (DLC)", 88000571, 88000572, 88000573, 88000574, 88000575, "Act Clear", true}},
    {"ActD_Petra", {"Arid Sands - Day Act 1", 88000576, 88000577, 88000578, 88000579, 88000580, "Act Clear", false}},
    {"ActD_SubPetra_04", {"Arid Sands - Day Act 1 - 2 (DLC)", 88000581, 88000582, 88000583, 88000584, 88000585, "Act Clear", true}},
    {"ActD_SubPetra_02", {"Arid Sands - Day Act 3 (DLC)", 88000586, 88000587, 88000588, 88000589, 88000590, "Act Clear", true}},
    {"ActN_PetraEvil", {"Arid Sands - Night Act 1", 88000591, 88000592, 88000593, 88000594, 88000595, "Act Clear", false}},
    {"ActN_SubPetra_03", {"Arid Sands - Night Act 2 (DLC)", 88000596, 88000597, 88000598, 88000599, 88000600, "Act Clear", true}},
    {"ActN_SubPetra_02", {"Arid Sands - Night Act 3 (DLC)", 88000601, 88000602, 88000603, 88000604, 88000605, "Act Clear", true}},
    {"BossPetra", {"Dark Guardian", 88000606, 88000607, 88000608, 88000609, 88000610, "Boss Clear", false}},
    {"ActD_Beach", {"Jungle Joyride - Day Act 1", 88000611, 88000612, 88000613, 88000614, 88000615, "Act Clear", false}},
    {"ActD_SubBeach_05", {"Jungle Joyride - Day Act 1 - 2 (DLC)", 88000616, 88000617, 88000618, 88000619, 88000620, "Act Clear", true}},
    {"ActD_SubBeach_01", {"Jungle Joyride - Day Act 4 (DLC)", 88000621, 88000622, 88000623, 88000624, 88000625, "Act Clear", true}},
    {"ActD_SubBeach_03", {"Jungle Joyride - Day Act 5 (DLC)", 88000626, 88000627, 88000628, 88000629, 88000630, "Act Clear", true}},
    {"BossEggLancer", {"Egg Lancer", 88000631, 88000632, 88000633, 88000634, 88000635, "Boss Clear", false}},
    {"ActN_BeachEvil", {"Jungle Joyride - Night Act 1", 88000636, 88000637, 88000638, 88000639, 88000640, "Act Clear", false}},
    {"ActN_SubBeach_03", {"Jungle Joyride - Night Act 1 - 2 (DLC)", 88000641, 88000642, 88000643, 88000644, 88000645, "Act Clear", true}},
    {"ActN_SubBeach_01", {"Jungle Joyride - Night Act 2", 88000646, 88000647, 88000648, 88000649, 88000650, "Act Clear", false}},
    {"ActN_SubBeach_02", {"Jungle Joyride - Night Act 3 (DLC)", 88000651, 88000652, 88000653, 88000654, 88000655, "Act Clear", true}},
    {"Act_EggmanLand", {"Eggmanland", 88000656, 88000657, 88000658, 88000659, 88000660, "Act Clear", false}},
    {"BossEggDragoon", {"Egg Dragoon", 88000661, 88000662, 88000663, 88000664, 88000665, "Boss Clear", false}},
    {"BossFinalDarkGaia", {"Final Dark Gaia", 88000666, 88000667, 88000668, 88000669, 88000670, "Boss Clear", false}},
// AP DISABLED:     {"ExStageTails2", {"Tornado Mission 2", 88000671, 88000672, 88000673, 88000674, 88000675, "Mission Clear", false}},
};
