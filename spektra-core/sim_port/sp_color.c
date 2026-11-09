/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sp_color — implementation (see sp_color.h). */
#include <math.h>

#include "sp_color.h"

/* ----------------------------------------------------------- transfer ----
 * Every curve has a log/gamma decode (host sample -> scene-linear) and the
 * matching encode (scene-linear -> host sample). The sRGB, 2.2-gamma and
 * ProPhoto implementations are byte-for-byte the ones the Phase-1 slice used,
 * so the default (sRGB) path is unchanged. The log curves use double math
 * internally for accuracy; they only run when explicitly selected. */

static float tf_srgb_to_linear(float c)
{
  if(!(c > 0.0f)) return 0.0f;
  if(c <= 0.04045f) return c / 12.92f;
  return powf((c + 0.055f) / 1.055f, 2.4f);
}
static float tf_linear_to_srgb(float c)
{
  if(!(c > 0.0f)) return 0.0f;
  if(c <= 0.0031308f) return c * 12.92f;
  return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}
static float tf_g22_to_linear(float v) { return v <= 0.0f ? 0.0f : powf(v, 2.2f); }
static float tf_linear_to_g22(float v) { return v <= 0.0f ? 0.0f : powf(v, 1.0f / 2.2f); }
static float tf_g24_to_linear(float v) { return v <= 0.0f ? 0.0f : powf(v, 2.4f); }
static float tf_linear_to_g24(float v) { return v <= 0.0f ? 0.0f : powf(v, 1.0f / 2.4f); }
static float tf_g26_to_linear(float v) { return v <= 0.0f ? 0.0f : powf(v, 2.6f); }
static float tf_linear_to_g26(float v) { return v <= 0.0f ? 0.0f : powf(v, 1.0f / 2.6f); }
/* ROMM RGB: linear below 1/512, else 1.8 gamma (threshold 16/512 encoded). */
static float tf_prophoto_to_linear(float v)
{
  if(v <= 0.0f) return 0.0f;
  return v <= 0.03125f ? v * (1.0f / 16.0f) : powf(v, 1.8f);
}
static float tf_linear_to_prophoto(float v)
{
  if(v <= 0.0f) return 0.0f;
  return v < 0.001953125f ? v * 16.0f : powf(v, 1.0f / 1.8f);
}

/* ARRI LogC4 (constants from the published spec, matching colour-science). */
static float tf_logc4_to_linear(float y)
{
  const double a = 2231.8263090676883, b = 0.9071358748778103;
  const double c = 0.09286412512218964, s = 0.1135972086105891;
  const double t = -0.01805699611991131;
  if((double)y >= 0.0) return (float)((pow(2.0, 14.0 * (((double)y - c) / b) + 6.0) - 64.0) / a);
  return (float)((double)y * s + t);
}
static float tf_linear_to_logc4(float x)
{
  const double a = 2231.8263090676883, b = 0.9071358748778103;
  const double c = 0.09286412512218964, s = 0.1135972086105891;
  const double t = -0.01805699611991131;
  if((double)x >= t) return (float)(((log2(a * (double)x + 64.0) - 6.0) / 14.0) * b + c);
  return (float)(((double)x - t) / s);
}

/* ARRI LogC3 (SUP 3.x, Linear Scene Exposure Factor, EI800). */
static float tf_logc3_to_linear(float t)
{
  const double cut = 0.010591, a = 5.555556, b = 0.052272;
  const double c = 0.24719, d = 0.385537, e = 5.367655, f = 0.092809;
  if((double)t > e * cut + f) return (float)((pow(10.0, ((double)t - d) / c) - b) / a);
  return (float)(((double)t - f) / e);
}
static float tf_linear_to_logc3(float x)
{
  const double cut = 0.010591, a = 5.555556, b = 0.052272;
  const double c = 0.24719, d = 0.385537, e = 5.367655, f = 0.092809;
  if((double)x > cut) return (float)(c * log10(a * (double)x + b) + d);
  return (float)(e * (double)x + f);
}

/* Blackmagic Film Generation 5. */
static float tf_bmd_to_linear(float y)
{
  const double A = 0.08692876065491224, B = 0.005494072432257808;
  const double C = 0.5300133392291939, D = 8.283605932402494;
  const double E = 0.09246575342465753, LIN_CUT = 0.005;
  if((double)y < D * LIN_CUT + E) return (float)(((double)y - E) / D);
  return (float)(exp(((double)y - C) / A) - B);
}
static float tf_linear_to_bmd(float x)
{
  const double A = 0.08692876065491224, B = 0.005494072432257808;
  const double C = 0.5300133392291939, D = 8.283605932402494;
  const double E = 0.09246575342465753, LIN_CUT = 0.005;
  if((double)x < LIN_CUT) return (float)(D * (double)x + E);
  return (float)(A * log((double)x + B) + C);
}

/* DaVinci Intermediate. */
static float tf_davinci_to_linear(float v)
{
  const double A = 0.0075, B = 7.0, C = 0.07329248;
  const double M = 10.44426855, LOG_CUT = 0.02740668;
  if((double)v <= LOG_CUT) return (float)((double)v / M);
  return (float)(pow(2.0, ((double)v / C) - B) - A);
}
static float tf_linear_to_davinci(float l)
{
  const double A = 0.0075, B = 7.0, C = 0.07329248;
  const double M = 10.44426855, LIN_CUT = 0.00262409;
  if((double)l <= LIN_CUT) return (float)((double)l * M);
  return (float)(C * (log2((double)l + A) + B));
}

/* RED Log3G10 v3. */
static float tf_log3g10_to_linear(float y)
{
  const double a = 0.224282, b = 155.975327, c = 0.01, g = 15.1927;
  if((double)y < 0.0) return (float)(((double)y / g) - c);
  return (float)((pow(10.0, fabs((double)y) / a) - 1.0) / b - c);
}
static float tf_linear_to_log3g10(float x)
{
  const double a = 0.224282, b = 155.975327, c = 0.01, g = 15.1927;
  const double v = (double)x + c;
  if(v < 0.0) return (float)(v * g);
  return (float)(a * log10(fabs(v) * b + 1.0));
}

/* Sony S-Log3. */
static float tf_slog3_to_linear(float y)
{
  const double d = (double)y * 1023.0;
  if((double)y >= 171.2102946929 / 1023.0)
    return (float)(pow(10.0, (d - 420.0) / 261.5) * 0.19 - 0.01);
  return (float)((d - 95.0) * 0.01125 / (171.2102946929 - 95.0));
}
static float tf_linear_to_slog3(float x)
{
  if((double)x >= 0.01125)
    return (float)((420.0 + log10(((double)x + 0.01) / 0.19) * 261.5) / 1023.0);
  return (float)(((double)x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0);
}

/* Canon Log 2 v1.2. */
static float tf_canonlog2_to_linear(float clog2)
{
  const double c = 0.24136077, b = 0.092864125, s = 87.09937546;
  double x;
  if((double)clog2 < b) x = -(pow(10.0, (b - (double)clog2) / c) - 1.0) / s;
  else x = (pow(10.0, ((double)clog2 - b) / c) - 1.0) / s;
  return (float)(x * 0.9);
}
static float tf_linear_to_canonlog2(float x)
{
  const double c = 0.24136077, b = 0.092864125, s = 87.09937546;
  const double u = (double)x / 0.9;
  if(u < 0.0) return (float)(b - c * log10(1.0 - u * s));
  return (float)(b + c * log10(u * s + 1.0));
}

/* Canon Log 3 v1.2. */
static float tf_canonlog3_to_linear(float clog3)
{
  double x;
  if((double)clog3 < 0.097465473)
    x = -(pow(10.0, (0.12783901 - (double)clog3) / 0.36726845) - 1.0) / 14.98325;
  else if((double)clog3 <= 0.15277891) x = ((double)clog3 - 0.12512219) / 1.9754798;
  else x = (pow(10.0, ((double)clog3 - 0.12240537) / 0.36726845) - 1.0) / 14.98325;
  return (float)(x * 0.9);
}
static float tf_linear_to_canonlog3(float x)
{
  const double u = (double)x / 0.9;
  if(u < -0.0139999998988)
    return (float)(0.12783901 - 0.36726845 * log10(1.0 - u * 14.98325));
  if(u <= 0.0140000014174) return (float)(u * 1.9754798 + 0.12512219);
  return (float)(0.12240537 + 0.36726845 * log10(u * 14.98325 + 1.0));
}

/* Panasonic V-Log. */
static float tf_vlog_to_linear(float v)
{
  if((double)v < 0.181) return (float)(((double)v - 0.125) / 5.6);
  return (float)(pow(10.0, ((double)v - 0.598206) / 0.241514) - 0.00873);
}
static float tf_linear_to_vlog(float l)
{
  if((double)l < 0.01) return (float)(5.6 * (double)l + 0.125);
  return (float)(0.241514 * log10((double)l + 0.00873) + 0.598206);
}

/* ACEScct. */
static float tf_acescct_to_linear(float x)
{
  const double A = 10.5402377416545, B = 0.0729055341958355, X_BRK = 0.0078125;
  if((double)x > X_BRK * A + B) return (float)(pow(2.0, (double)x * 17.52 - 9.72));
  return (float)(((double)x - B) / A);
}
static float tf_linear_to_acescct(float l)
{
  const double A = 10.5402377416545, B = 0.0729055341958355, X_BRK = 0.0078125;
  if((double)l <= X_BRK) return (float)(A * (double)l + B);
  return (float)((log2((double)l) + 9.72) / 17.52);
}

/* ACEScc. */
static float tf_acescc_to_linear(float x)
{
  const double hi = (log2(65504.0) + 9.72) / 17.52;
  const double lo = (9.72 - 15.0) / 17.52;
  if((double)x >= hi) return 65504.0f;
  if((double)x < lo)
    return (float)((pow(2.0, (double)x * 17.52 - 9.72) - pow(2.0, -16.0)) * 2.0);
  return (float)(pow(2.0, (double)x * 17.52 - 9.72));
}
static float tf_linear_to_acescc(float l)
{
  const double x = (double)l;
  if(x < 0.0) return (float)((log2(pow(2.0, -16.0)) + 9.72) / 17.52);
  if(x < pow(2.0, -15.0))
    return (float)((log2(pow(2.0, -16.0) + x * 0.5) + 9.72) / 17.52);
  return (float)((log2(x) + 9.72) / 17.52);
}

float sp_transfer_decode(int t, float v)
{
  switch(t)
  {
    case SP_TRANSFER_LINEAR: return v;
    case SP_TRANSFER_G22: return tf_g22_to_linear(v);
    case SP_TRANSFER_PROPHOTO: return tf_prophoto_to_linear(v);
    case SP_TRANSFER_G24: return tf_g24_to_linear(v);
    case SP_TRANSFER_G26: return tf_g26_to_linear(v);
    case SP_TRANSFER_LOGC4: return tf_logc4_to_linear(v);
    case SP_TRANSFER_LOGC3: return tf_logc3_to_linear(v);
    case SP_TRANSFER_BMDFILM: return tf_bmd_to_linear(v);
    case SP_TRANSFER_DAVINCI: return tf_davinci_to_linear(v);
    case SP_TRANSFER_LOG3G10: return tf_log3g10_to_linear(v);
    case SP_TRANSFER_SLOG3: return tf_slog3_to_linear(v);
    case SP_TRANSFER_CANONLOG2: return tf_canonlog2_to_linear(v);
    case SP_TRANSFER_CANONLOG3: return tf_canonlog3_to_linear(v);
    case SP_TRANSFER_VLOG: return tf_vlog_to_linear(v);
    case SP_TRANSFER_ACES_CCT: return tf_acescct_to_linear(v);
    case SP_TRANSFER_ACES_CC: return tf_acescc_to_linear(v);
    default: return tf_srgb_to_linear(v);
  }
}

float sp_transfer_encode(int t, float v)
{
  switch(t)
  {
    case SP_TRANSFER_LINEAR: return v;
    case SP_TRANSFER_G22: return tf_linear_to_g22(v);
    case SP_TRANSFER_PROPHOTO: return tf_linear_to_prophoto(v);
    case SP_TRANSFER_G24: return tf_linear_to_g24(v);
    case SP_TRANSFER_G26: return tf_linear_to_g26(v);
    case SP_TRANSFER_LOGC4: return tf_linear_to_logc4(v);
    case SP_TRANSFER_LOGC3: return tf_linear_to_logc3(v);
    case SP_TRANSFER_BMDFILM: return tf_linear_to_bmd(v);
    case SP_TRANSFER_DAVINCI: return tf_linear_to_davinci(v);
    case SP_TRANSFER_LOG3G10: return tf_linear_to_log3g10(v);
    case SP_TRANSFER_SLOG3: return tf_linear_to_slog3(v);
    case SP_TRANSFER_CANONLOG2: return tf_linear_to_canonlog2(v);
    case SP_TRANSFER_CANONLOG3: return tf_linear_to_canonlog3(v);
    case SP_TRANSFER_VLOG: return tf_linear_to_vlog(v);
    case SP_TRANSFER_ACES_CCT: return tf_linear_to_acescct(v);
    case SP_TRANSFER_ACES_CC: return tf_linear_to_acescc(v);
    default: return tf_linear_to_srgb(v);
  }
}

int sp_transfer_count(void) { return (int)SP_TRANSFER_COUNT; }

const char *sp_transfer_label(int index)
{
  static const char *const labels[SP_TRANSFER_COUNT] = {
      "sRGB",            "Linear",          "Gamma 2.2",     "ProPhoto RGB",
      "Gamma 2.4",       "Gamma 2.6",       "ARRI LogC4",    "ARRI LogC3 EI800",
      "BMD Film Gen 5",  "DaVinci Interm.", "RED Log3G10",   "Sony S-Log3",
      "Canon Log 2",     "Canon Log 3",     "Panasonic V-Log", "ACEScct",
      "ACEScc"};
  if(index < 0 || index >= (int)SP_TRANSFER_COUNT) index = SP_TRANSFER_SRGB;
  return labels[index];
}

/* ------------------------------------------------------------ primaries --
 * Native-white RGB->XYZ matrices (XYZ = M * RGB, row-major) for the input
 * gamuts. Index 0 (sRGB) is the published matrix the Phase-1 slice used, so it
 * stays byte-identical; the rest are primaries-derived (as the OFX generator's
 * colour.RGB_to_XYZ). white_xy is the space's own white point, which the engine
 * CAT16-adapts to the table illuminant. */
typedef struct
{
  const char *label;
  double rgb_to_xyz[9];
  double white_xy[2];
} sp_color_space_t;

static const sp_color_space_t SP_COLOR_SPACES[] = {
    {"sRGB",
     {0.4124, 0.3576, 0.1805, 0.2126, 0.7152, 0.0722, 0.0193, 0.1192, 0.9505},
     {0.3127, 0.3290}},
    {"Display P3",
     {0.4865709486, 0.2656676932, 0.1982172852, 0.2289745641, 0.6917385218,
      0.0792869141, -0.0, 0.0451133819, 1.0439443689},
     {0.3127, 0.3290}},
    {"DCI-P3",
     {0.4451698156, 0.2771344092, 0.1722826698, 0.2094916779, 0.7215952542,
      0.0689130679, -0.0, 0.0470605601, 0.9073553944},
     {0.3140, 0.3510}},
    {"P3-D65",
     {0.4865709486, 0.2656676932, 0.1982172852, 0.2289745641, 0.6917385218,
      0.0792869141, -0.0, 0.0451133819, 1.0439443689},
     {0.3127, 0.3290}},
    {"Rec.709",
     {0.4123907993, 0.3575843394, 0.1804807884, 0.2126390059, 0.7151686788,
      0.0721923154, 0.0193308187, 0.1191947798, 0.9505321522},
     {0.3127, 0.3290}},
    {"Rec.2020",
     {0.6369580483, 0.1446169036, 0.1688809752, 0.2627002120, 0.6779980715,
      0.0593017165, 0.0, 0.0280726930, 1.0609850577},
     {0.3127, 0.3290}},
    {"Adobe RGB (1998)",
     {0.57667, 0.18556, 0.18823, 0.29734, 0.62736, 0.07529, 0.02703, 0.07069,
      0.99134},
     {0.3127, 0.3290}},
    {"ProPhoto RGB",
     {0.7977, 0.1352, 0.0313, 0.288, 0.7119, 0.0001, -0.0, -0.0, 0.8249},
     {0.3457, 0.3585}},
    {"ACES2065-1",
     {0.9525523959, -0.0, 0.0000936786, 0.3439664498, 0.7281660966,
      -0.0721325464, -0.0, -0.0, 1.0088251844},
     {0.32168, 0.33767}},
    {"ACEScg",
     {0.6624541811, 0.1340042065, 0.1561876870, 0.2722287168, 0.6740817658,
      0.0536895174, -0.0055746495, 0.0040607335, 1.0103391003},
     {0.32168, 0.33767}},
    {"ARRI Wide Gamut 4",
     {0.7048583204, 0.1297602952, 0.1158373115, 0.2545241764, 0.7814777327,
      -0.0360019091, -0.0, -0.0, 1.0890577508},
     {0.3127, 0.3290}},
    {"ARRI Wide Gamut 3",
     {0.638008, 0.214704, 0.097744, 0.291954, 0.823841, -0.115795, 0.002798,
      -0.067034, 1.153294},
     {0.3127, 0.3290}},
    {"Blackmagic Wide Gamut",
     {0.6065303722, 0.2204080953, 0.1234790005, 0.2679894071, 0.8327308786,
      -0.1007202857, -0.0294421660, -0.0866106070, 1.2048607644},
     {0.312717, 0.329031}},
    {"DaVinci Wide Gamut",
     {0.7006223921, 0.1487748151, 0.1010587198, 0.2741185109, 0.8736318959,
      -0.1477504068, -0.0989629129, -0.1378953251, 1.3259159887},
     {0.3127, 0.3290}},
    {"RED Wide Gamut RGB",
     {0.735275, 0.068609, 0.146571, 0.286694, 0.842979, -0.129673, -0.079681,
      -0.347343, 1.516082},
     {0.3127, 0.3290}},
    {"S-Gamut3",
     {0.7064827132, 0.1288010498, 0.1151721641, 0.2709796708, 0.7866064112,
      -0.0575860820, -0.0096778454, 0.0046000375, 1.0941355587},
     {0.3127, 0.3290}},
    {"S-Gamut3.Cine",
     {0.5990839208, 0.2489255161, 0.1024464902, 0.2150758201, 0.8850685017,
      -0.1001443219, -0.0320658495, -0.0276583907, 1.1487819910},
     {0.3127, 0.3290}},
    {"Cinema Gamut",
     {0.7160496466, 0.1296834779, 0.1047228026, 0.2612613575, 0.8696421458,
      -0.1309035033, -0.0096763466, -0.2364816361, 1.3352157335},
     {0.3127, 0.3290}},
    {"V-Gamut",
     {0.679644, 0.152211, 0.118600, 0.260686, 0.774894, -0.035580, -0.009310,
      -0.004612, 1.102980},
     {0.3127, 0.3290}},
};

#define SP_COLOR_SPACE_COUNT \
  ((int)(sizeof(SP_COLOR_SPACES) / sizeof(SP_COLOR_SPACES[0])))

int sp_color_space_count(void) { return SP_COLOR_SPACE_COUNT; }

const char *sp_color_space_label(int index)
{
  if(index < 0 || index >= SP_COLOR_SPACE_COUNT) index = 0;
  return SP_COLOR_SPACES[index].label;
}

void sp_color_space_matrix(int index, double rgb_to_xyz[9], double white_xy[2])
{
  if(index < 0 || index >= SP_COLOR_SPACE_COUNT) index = 0;
  for(int i = 0; i < 9; i++) rgb_to_xyz[i] = SP_COLOR_SPACES[index].rgb_to_xyz[i];
  white_xy[0] = SP_COLOR_SPACES[index].white_xy[0];
  white_xy[1] = SP_COLOR_SPACES[index].white_xy[1];
}
