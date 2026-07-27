#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <hdf5.h>

namespace sca {

/// Streaming HDF5 writer matching SCAM's trace-database layout.
///
/// Layout:
///   experiment/series/samples      (N, S) or (N, S, C)  float32
///   experiment/series/timestamps    (N,)  variable-length string
///   experiment/series/<meta_key>    (N,)  variable-length string  (dynamic)
///
/// Known metadata keys are mapped to SCAM dataset names:
///   "plaintext"  -> "stimuli"
///   "ciphertext" -> "responses"
///   "key"        -> "keys"
/// All other keys create a dataset with the same name.
class Hdf5Writer {
public:
    Hdf5Writer(const std::string& path,
               const std::string& experiment,
               const std::string& series,
               size_t sample_len,
               const std::vector<size_t>& sample_shape);

    /// Append one trace.  @p metadata maps field names to values.
    void write_trace(const float* samples,
                     const std::string& timestamp,
                     const std::map<std::string, std::string>& metadata);

    /// Set a string attribute on the series group.
    void set_attribute(const std::string& key, const std::string& val);

    /// Write a 1-D string dataset (e.g. signal names).
    void write_string_dataset(const std::string& name,
                              const std::vector<std::string>& values);

    void close();
    ~Hdf5Writer();

    Hdf5Writer(const Hdf5Writer&) = delete;
    Hdf5Writer& operator=(const Hdf5Writer&) = delete;

    static constexpr hsize_t kChunk = 1000;

private:
    void grow_if_needed(const std::map<std::string, std::string>& metadata);
    hid_t get_or_create_string_ds(const std::string& name);

    /// Map a user-facing metadata key to the HDF5 dataset name.
    static std::string dataset_name(const std::string& key);

    hid_t file_       = -1;
    hid_t series_grp_ = -1;
    hid_t ds_samples_ = -1;
    hid_t ds_ts_      = -1;
    hid_t str_type_   = -1;

    /// Lazily-created string datasets keyed by HDF5 dataset name.
    std::map<std::string, hid_t> meta_ds_;

    size_t sample_len_ = 0;
    std::vector<size_t> sample_shape_;
    hsize_t trace_count_  = 0;
    hsize_t allocated_    = 0;
    bool closed_ = false;
};

} // namespace sca
