#include "utils/viewer.hh"

#include <cstddef>

#include "cusz/review.h"
#include "cusz/type.h"
#include "header.h"
#include "mem/layout_cxx.hh"
#include "tehm.hh"

float get_throughput(float milliseconds, size_t nbyte)
{
  auto GiB = 1.0 * 1024 * 1024 * 1024;
  auto seconds = milliseconds * 1e-3;
  return nbyte / GiB / seconds;
}

void println_throughput(const char* s, float timer, size_t _nbyte)
{
  if (timer == 0.0) return;
  auto t = get_throughput(timer, _nbyte);
  printf("%-12s %'14f %'11.2f\n", s, timer, t);
};

void println_throughput_tablehead()
{
  printf(
      "\n\e[1m\e[31m%-12s %14s %11s\e[0m\n",  //
      const_cast<char*>("kernel"),            //
      const_cast<char*>("time, ms"),          //
      const_cast<char*>("GiB/s")              //
  );
}

double get_total_time(psz::timerecord_t r)
{
  double total = 0.0;
  std::for_each(r->begin(), r->end(), [&](psz::TimeRecordTuple t) {
    return total += std::get<1>(t);
  });
  return total;
}

void* psz_make_timerecord() { return (void*)new psz::TimeRecord; }

void psz_review_comp_time_breakdown(void* _r, psz_header* h)
{
  auto sizeof_T = [&]() { return (h->dtype == F4 ? 4 : 8); };
  auto uncomp_bytes = h->x * h->y * h->z * sizeof_T();

  auto r = (psz::timerecord_t)_r;

  psz::TimeRecord reflow;

  {  // reflow
    psz::TimeRecordTuple book_tuple;

    auto total_time = get_total_time(r);
    auto subtotal_time = total_time;

    for (auto& i : *r) {
      auto item = std::string(std::get<0>(i));
      if (item == "book") {
        book_tuple = i;
        subtotal_time -= std::get<1>(i);
      }
      else {
        reflow.push_back(i);
      }
    }
    reflow.push_back({const_cast<const char*>("(subtotal)"), subtotal_time});
    printf("\e[2m");
    reflow.push_back(book_tuple);
    reflow.push_back({const_cast<const char*>("(total)"), total_time});
    printf("\e[0m");
  }

  println_throughput_tablehead();
  for (auto& i : reflow)
    println_throughput(std::get<0>(i), std::get<1>(i), uncomp_bytes);

  printf("\n");
}

void psz_review_from_header(psz_header* h)
{
  // [TODO] put error status
  if (h->dtype != F4 and h->dtype != F8)
    cout << "[psz::log::fatal_error] original length is is zero." << endl;

  auto comp_bytes = [&]() {
    auto END = sizeof(h->entry) / sizeof(h->entry[0]);
    return h->entry[END - 1];
  };

  auto sizeof_T = [&]() { return (h->dtype == F4 ? 4 : 8); };
  auto uncomp_bytes = h->x * h->y * h->z * sizeof_T();
  auto fieldsize = [&](auto FIELD) {
    return h->entry[FIELD + 1] - h->entry[FIELD];
  };
  auto __print = [&](auto str, auto num) {
    // cout << "  ";
    cout << std::left;
    cout << std::setw(25) << string(str);
    cout << std::right;
    cout << std::setw(14) << num;
    cout << '\n';
  };
  auto __print_perc = [&](auto str, auto num) {
    auto perc = num * 100.0 / comp_bytes();
    // cout << "  ";
    cout << std::left;
    cout << std::setw(25) << string(str);
    cout << std::right;
    cout << std::setw(14) << num;
    cout << std::setw(10) << std::setprecision(3) << std::fixed << perc
         << "%\n";
  };
  auto __newline = []() { cout << '\n'; };

  auto n_outlier =
      fieldsize(PSZHEADER_SPFMT) / (sizeof_T() + sizeof(uint32_t));

  __print(
      "logging::predictor",
      h->logging_pred_type == Lorenzo ? "Lorenzo" : "Spine");
  __print("logging::max", h->logging_max);
  __print("logging::min", h->logging_min);
  __print("logging::range", h->logging_max - h->logging_min);
  __print("logging::mode", h->logging_mode == Rel ? "Rel" : "Abs");
  __print("logging::input_eb", h->logging_input_eb);
  __print("logging::final_eb", h->logging_final_eb);
  printf("--------------------------------------------------\n");
  if (comp_bytes() != 0) {
    auto cr = 1.0 * uncomp_bytes / comp_bytes();
    // __newline();
    __print("data::comp_metric::CR", cr);
  }
  else {
    cout << "[psz::log::fatal_error] compressed len is zero." << endl;
  }
  __print("data::original_bytes", uncomp_bytes);
  __print_perc("data::comp_bytes", comp_bytes());
  printf("--------------------------------------------------\n");
  __print_perc("file::header::bytes", fieldsize(PSZHEADER_HEADER));
  __print_perc("file::anchor::bytes", fieldsize(PSZHEADER_ANCHOR));
  __print_perc("file::huffman::bytes", fieldsize(PSZHEADER_VLE));
  __print_perc("file::outlier::bytes", fieldsize(PSZHEADER_SPFMT));
  printf("--------------------------------------------------\n");
  __print("file::anchor:::number", fieldsize(PSZHEADER_ANCHOR) / sizeof_T());
  __print("file::outlier:::number", n_outlier);
}

void psz_review_compression(void* r, psz_header* h)
{
  printf("\n(c) COMPRESSION REPORT\n");
  psz_review_from_header(h);
  psz_review_comp_time_breakdown((psz::timerecord_t)r, h);
}

void psz_review_decompression(void* r, size_t bytes)
{
  printf("\n(d) deCOMPRESSION REPORT\n");

  auto total_time = get_total_time((psz::timerecord_t)r);
  (*(psz::timerecord_t)r)
      .push_back({const_cast<const char*>("(total)"), total_time});

  println_throughput_tablehead();
  for (auto& i : *(psz::timerecord_t)r)
    println_throughput(std::get<0>(i), std::get<1>(i), bytes);

  printf("\n");
}

template <typename T>
void psz::print_metrics_cross(
    psz_summary* s, size_t comp_bytes, bool gpu_checker)
{
  auto checker =
      (not gpu_checker) ? string("CPU-checker") : string("GPU-checker");

  auto bytes = (s->len * sizeof(T) * 1.0);
  auto is_fp = std::is_floating_point_v<T> ? const_cast<char*>("yes")
                                           : const_cast<char*>("no");

  double to_KiB = 1024.0, to_MiB = 1024.0 * to_KiB, to_GiB = 1024.0 * to_MiB;

  auto println_v2 = [](string const prefix, string const kw, double n1,
                       bool rounding_to_4th = false) {
    std::string combined = prefix + "::\e[1m\e[31m" + kw + "\e[0m";
    if (not rounding_to_4th)
      printf("%-*s%16.8g\n", 36, combined.c_str(), n1);
    else
      printf("%-*s%16.4f\n", 36, combined.c_str(), n1);
  };

  auto println_text_v2 = [](string const prefix, string const kw,
                            string const text) {
    std::string combined = prefix + "::\e[1m\e[31m" + kw + "\e[0m";
    printf("%-*s%*s\n", 36, combined.c_str(), 16, text.c_str());
  };

  auto println_segline = []() {
    printf("---------------------------------------\n");
  };

  string dtype_text = !is_fp ? "non-fp" : sizeof(T) == 4 ? "fp32" : "fp64";

  println_text_v2("status", "checker", checker);
  println_segline();
  println_v2("data", "length", s->len);
  println_text_v2("data", "dtype", dtype_text);
  println_segline();
  println_v2("data", "original_bytes", bytes);
  println_v2("data", "original_KiB", bytes / to_KiB, true);
  println_v2("data", "original_MiB", bytes / to_MiB, true);
  println_v2("data", "original_GiB", bytes / to_GiB, true);
  println_segline();
  println_v2("data", "comp_bytes", comp_bytes);
  println_v2("data", "compressed_KiB", comp_bytes / to_KiB, true);
  println_v2("data", "compressed_MiB", comp_bytes / to_MiB, true);
  println_v2("data", "compressed_GiB", comp_bytes / to_GiB, true);
  println_segline();
  println_v2("comp_metric", "CR", bytes / comp_bytes);
  println_v2("comp_metric", "bitrate", 32.0 / (bytes / comp_bytes));
  println_v2("comp_metric", "NRMSE", s->score_NRMSE);
  println_v2("comp_metric", "coeff", s->score_coeff);
  println_v2("comp_metric", "PSNR", s->score_PSNR);
  println_segline();
  println_v2("data_original", "min", s->odata.min);
  println_v2("data_original", "max", s->odata.max);
  println_v2("data_original", "rng", s->odata.rng);
  println_v2("data_original", "avg", s->odata.avg);
  println_v2("data_original", "std", s->odata.std);
  println_segline();
  println_v2("data_decompressed", "min", s->xdata.min);
  println_v2("data_decompressed", "max", s->xdata.max);
  println_v2("data_decompressed", "rng", s->xdata.rng);
  println_v2("data_decompressed", "avg", s->xdata.avg);
  println_v2("data_decompressed", "std", s->xdata.std);
  println_segline();
  println_v2("data_max_diff", "index", s->max_err_idx);
  println_v2("data_max_diff", "value", s->max_err_abs);
  println_v2("data_max_diff", "vs_rng", s->max_err_rel);
  println_segline();
}

void psz::print_metrics_auto(double* lag1_cor, double* lag2_cor)
{
  auto println_v2 = [](string const prefix, string const kw, double n1) {
    std::string combined = prefix + "::\e[1m\e[31m" + kw + "\e[0m";
    printf("%-*s%16.8g\n", 36, combined.c_str(), n1);
  };

  println_v2("autocorrelation", "lag1", *lag1_cor);
  println_v2("autocorrelation", "lag2", *lag2_cor);
}

template <typename T>
void pszcxx_evaluate_quality_cpu(
    T* _d1, T* _d2, size_t len, size_t comp_bytes, bool from_device)
{
  auto stat = new psz_summary;
  T* reconstructed;
  T* origin;
  if (not from_device) {
    reconstructed = _d1;
    origin = _d2;
  }
  else {
    printf("allocating tmp space for CPU verification\n");
    auto bytes = sizeof(T) * len;
    GpuMallocHost(&reconstructed, bytes);
    GpuMallocHost(&origin, bytes);
    GpuMemcpy(reconstructed, _d1, bytes, GpuMemcpyD2H);
    GpuMemcpy(origin, _d2, bytes, GpuMemcpyD2H);
  }
  cusz::verify_data<T>(stat, reconstructed, origin, len);
  psz::print_metrics_cross<T>(stat, comp_bytes, false);

  auto stat_auto_lag1 = new psz_summary;
  cusz::verify_data<T>(stat_auto_lag1, origin, origin + 1, len - 1);
  auto stat_auto_lag2 = new psz_summary;
  cusz::verify_data<T>(stat_auto_lag2, origin, origin + 2, len - 2);

  psz::print_metrics_auto(
      &stat_auto_lag1->score_coeff, &stat_auto_lag2->score_coeff);

  if (from_device) {
    if (reconstructed) GpuFreeHost(reconstructed);
    if (origin) GpuFreeHost(origin);
  }

  delete stat, delete stat_auto_lag1, delete stat_auto_lag2;
}

template <typename T>
void psz::view(
    psz_header* header, memobj<T>* xdata, memobj<T>* cmp,
    string const& file_to_compare)
{
  auto len = pszheader_uncompressed_len(header);
  auto compressd_bytes = pszheader_compressed_len(header);

  auto compare_on_gpu = [&]() {
    cmp->control({MallocHost, Malloc})
        ->file(file_to_compare.c_str(), FromFile)
        ->control({H2D});

    pszcxx_evaluate_quality_gpu(
        xdata->dptr(), cmp->dptr(), len, compressd_bytes);
  };

  auto compare_on_cpu = [&]() {
    cmp->control({MallocHost})->file(file_to_compare.c_str(), FromFile);
    xdata->control({D2H});
    pszcxx_evaluate_quality_cpu(
        xdata->hptr(), cmp->hptr(), len, compressd_bytes);
  };

  if (file_to_compare != "") {
    auto gb = 1.0 * sizeof(T) * len / 1e9;
    if (gb < 0.8)
      compare_on_gpu();
    else
      compare_on_cpu();
  }
}

#define __INSTANTIATE_CPU_VIEWER(T) \
  template void psz::view<T>(       \
      psz_header*, memobj<T>*, memobj<T>*, string const&);

__INSTANTIATE_CPU_VIEWER(float)
__INSTANTIATE_CPU_VIEWER(double)

void psz_review_evaluated_quality(
    psz_policy p, psz_dtype d, void* xdata, void* odata, size_t len,
    size_t comp_bytes, bool arrays_on_device)
{
  if (d == F4) {
    auto x = (float*)xdata;
    auto o = (float*)odata;
    if (p == THRUST)
      pszcxx_evaluate_quality_gpu<float, THRUST>(x, o, len, comp_bytes);
    else if (p == PROPER_GPU_BACKEND)
      pszcxx_evaluate_quality_gpu<float, PROPER_GPU_BACKEND>(
          x, o, len, comp_bytes);
    else if (p == SEQ)
      pszcxx_evaluate_quality_cpu<float>(
          x, o, len, comp_bytes, arrays_on_device);
    else
      printf("psz_review_evaluated_quality: not a valid backend.");
  }
  else if (d == F8) {
    auto x = (double*)xdata;
    auto o = (double*)odata;
    if (p == THRUST)
      pszcxx_evaluate_quality_gpu<double, THRUST>(x, o, len, comp_bytes);
    else if (p == PROPER_GPU_BACKEND)
      pszcxx_evaluate_quality_gpu<double, PROPER_GPU_BACKEND>(
          x, o, len, comp_bytes);
    else if (p == SEQ)
      pszcxx_evaluate_quality_cpu<double>(
          x, o, len, comp_bytes, arrays_on_device);
    else
      printf("psz_review_evaluated_quality: not a valid backend.");
  }
}
