#include "sca/hdf5_writer.h"

#include <stdexcept>

namespace sca {

// --------------- helpers ---------------

static hid_t create_vlen_string_type() {
    hid_t t = H5Tcopy(H5T_C_S1);
    H5Tset_size(t, H5T_VARIABLE);
    H5Tset_cset(t, H5T_CSET_UTF8);
    return t;
}

static hid_t create_string_dataset(hid_t group, const char* name,
                                   hid_t str_type, hsize_t init_rows) {
    hsize_t dims[1]    = {init_rows};
    hsize_t maxdims[1] = {H5S_UNLIMITED};
    hid_t space = H5Screate_simple(1, dims, maxdims);

    hid_t plist = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk[1] = {Hdf5Writer::kChunk};
    H5Pset_chunk(plist, 1, chunk);

    hid_t ds = H5Dcreate2(group, name, str_type, space,
                           H5P_DEFAULT, plist, H5P_DEFAULT);
    H5Pclose(plist);
    H5Sclose(space);
    return ds;
}

static void write_string_at(hid_t ds, hid_t str_type,
                             hsize_t row, const std::string& val) {
    hsize_t count[1]  = {1};
    hsize_t offset[1] = {row};
    hid_t fspace = H5Dget_space(ds);
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, offset, nullptr, count, nullptr);

    hid_t mspace = H5Screate_simple(1, count, nullptr);
    const char* ptr = val.c_str();
    H5Dwrite(ds, str_type, mspace, fspace, H5P_DEFAULT, &ptr);

    H5Sclose(mspace);
    H5Sclose(fspace);
}

static void resize_string_ds(hid_t ds, hsize_t new_rows) {
    hsize_t dims[1] = {new_rows};
    H5Dset_extent(ds, dims);
}

// --------------- Hdf5Writer ---------------

std::string Hdf5Writer::dataset_name(const std::string& key) {
    if (key == "plaintext")  return "stimuli";
    if (key == "ciphertext") return "responses";
    if (key == "key")        return "keys";
    return key;
}

Hdf5Writer::Hdf5Writer(const std::string& path,
                       const std::string& experiment,
                       const std::string& series,
                       size_t sample_len,
                       const std::vector<size_t>& sample_shape)
    : sample_len_(sample_len), sample_shape_(sample_shape), allocated_(kChunk)
{
    // Open existing file or create a new one.
    {
        H5E_auto2_t old_func; void* old_data;
        H5Eget_auto2(H5E_DEFAULT, &old_func, &old_data);
        H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
        file_ = H5Fopen(path.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
        H5Eset_auto2(H5E_DEFAULT, old_func, old_data);
    }
    if (file_ < 0) {
        file_ = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        if (file_ < 0)
            throw std::runtime_error("Hdf5Writer: cannot create " + path);
    }

    // Open or create experiment group.
    hid_t exp_grp;
    {
        H5E_auto2_t old_func; void* old_data;
        H5Eget_auto2(H5E_DEFAULT, &old_func, &old_data);
        H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
        exp_grp = H5Gopen2(file_, experiment.c_str(), H5P_DEFAULT);
        H5Eset_auto2(H5E_DEFAULT, old_func, old_data);
    }
    if (exp_grp < 0)
        exp_grp = H5Gcreate2(file_, experiment.c_str(),
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    series_grp_ = H5Gcreate2(exp_grp, series.c_str(),
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Gclose(exp_grp);

    // --- samples dataset: (N, <sample_shape>) float32 ---
    {
        size_t rank = 1 + sample_shape_.size();
        std::vector<hsize_t> dims(rank), maxdims(rank), chunk(rank);
        dims[0]    = allocated_;
        maxdims[0] = H5S_UNLIMITED;
        chunk[0]   = kChunk;
        for (size_t i = 0; i < sample_shape_.size(); ++i) {
            dims[i + 1]    = sample_shape_[i];
            maxdims[i + 1] = sample_shape_[i];
            chunk[i + 1]   = sample_shape_[i];
        }

        hid_t space = H5Screate_simple(static_cast<int>(rank),
                                        dims.data(), maxdims.data());
        hid_t plist = H5Pcreate(H5P_DATASET_CREATE);
        H5Pset_chunk(plist, static_cast<int>(rank), chunk.data());

        ds_samples_ = H5Dcreate2(series_grp_, "samples",
                                  H5T_IEEE_F32LE, space,
                                  H5P_DEFAULT, plist, H5P_DEFAULT);
        H5Pclose(plist);
        H5Sclose(space);
    }

    // --- timestamps dataset ---
    str_type_ = create_vlen_string_type();
    ds_ts_ = create_string_dataset(series_grp_, "timestamps", str_type_, allocated_);

    // Initial trace_count attribute.
    {
        hid_t scalar = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(series_grp_, "trace_count",
                                 H5T_NATIVE_HSIZE, scalar,
                                 H5P_DEFAULT, H5P_DEFAULT);
        H5Awrite(attr, H5T_NATIVE_HSIZE, &trace_count_);
        H5Aclose(attr);
        H5Sclose(scalar);
    }
}

hid_t Hdf5Writer::get_or_create_string_ds(const std::string& name) {
    auto it = meta_ds_.find(name);
    if (it != meta_ds_.end()) return it->second;

    hid_t ds = create_string_dataset(series_grp_, name.c_str(),
                                     str_type_, allocated_);
    meta_ds_[name] = ds;
    return ds;
}

void Hdf5Writer::grow_if_needed(
        const std::map<std::string, std::string>& metadata) {
    if (trace_count_ < allocated_) return;

    allocated_ += kChunk;

    // Grow samples.
    {
        size_t rank = 1 + sample_shape_.size();
        std::vector<hsize_t> dims(rank);
        dims[0] = allocated_;
        for (size_t i = 0; i < sample_shape_.size(); ++i)
            dims[i + 1] = sample_shape_[i];
        H5Dset_extent(ds_samples_, dims.data());
    }

    // Grow timestamps.
    resize_string_ds(ds_ts_, allocated_);

    // Grow all existing metadata datasets.
    for (auto& [name, ds] : meta_ds_)
        resize_string_ds(ds, allocated_);

    // Ensure datasets exist for any new metadata keys.
    for (const auto& [k, v] : metadata)
        get_or_create_string_ds(dataset_name(k));
}

void Hdf5Writer::write_trace(const float* samples,
                              const std::string& timestamp,
                              const std::map<std::string, std::string>& metadata) {
    grow_if_needed(metadata);

    // Write samples row.
    {
        size_t rank = 1 + sample_shape_.size();
        std::vector<hsize_t> count(rank), offset(rank);
        offset[0] = trace_count_;
        count[0]  = 1;
        for (size_t i = 0; i < sample_shape_.size(); ++i) {
            offset[i + 1] = 0;
            count[i + 1]  = sample_shape_[i];
        }

        hid_t fspace = H5Dget_space(ds_samples_);
        H5Sselect_hyperslab(fspace, H5S_SELECT_SET,
                             offset.data(), nullptr, count.data(), nullptr);

        hid_t mspace = H5Screate_simple(static_cast<int>(rank), count.data(), nullptr);
        H5Dwrite(ds_samples_, H5T_NATIVE_FLOAT, mspace, fspace,
                 H5P_DEFAULT, samples);
        H5Sclose(mspace);
        H5Sclose(fspace);
    }

    // Write timestamp.
    write_string_at(ds_ts_, str_type_, trace_count_, timestamp);

    // Write metadata fields.
    for (const auto& [k, v] : metadata) {
        hid_t ds = get_or_create_string_ds(dataset_name(k));
        write_string_at(ds, str_type_, trace_count_, v);
    }

    ++trace_count_;

    // Update trace_count attribute.
    {
        hid_t attr = H5Aopen(series_grp_, "trace_count", H5P_DEFAULT);
        H5Awrite(attr, H5T_NATIVE_HSIZE, &trace_count_);
        H5Aclose(attr);
    }
}

void Hdf5Writer::set_attribute(const std::string& key,
                                const std::string& val) {
    hid_t scalar = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(series_grp_, key.c_str(),
                             str_type_, scalar,
                             H5P_DEFAULT, H5P_DEFAULT);
    const char* ptr = val.c_str();
    H5Awrite(attr, str_type_, &ptr);
    H5Aclose(attr);
    H5Sclose(scalar);
}

void Hdf5Writer::write_string_dataset(const std::string& name,
                                       const std::vector<std::string>& values) {
    hsize_t dims[1] = {values.size()};
    hid_t space = H5Screate_simple(1, dims, nullptr);

    hid_t plist = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk[1] = {std::min(values.size(), static_cast<size_t>(kChunk))};
    H5Pset_chunk(plist, 1, chunk);

    hid_t ds = H5Dcreate2(series_grp_, name.c_str(), str_type_, space,
                           H5P_DEFAULT, plist, H5P_DEFAULT);

    std::vector<const char*> ptrs(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        ptrs[i] = values[i].c_str();

    H5Dwrite(ds, str_type_, H5S_ALL, H5S_ALL, H5P_DEFAULT, ptrs.data());

    H5Dclose(ds);
    H5Pclose(plist);
    H5Sclose(space);
}

void Hdf5Writer::close() {
    if (closed_) return;
    closed_ = true;

    // Trim datasets to actual trace_count.
    {
        size_t rank = 1 + sample_shape_.size();
        std::vector<hsize_t> dims(rank);
        dims[0] = trace_count_;
        for (size_t i = 0; i < sample_shape_.size(); ++i)
            dims[i + 1] = sample_shape_[i];
        H5Dset_extent(ds_samples_, dims.data());
    }
    resize_string_ds(ds_ts_, trace_count_);
    for (auto& [name, ds] : meta_ds_)
        resize_string_ds(ds, trace_count_);

    // Close handles.
    H5Dclose(ds_samples_);
    H5Dclose(ds_ts_);
    for (auto& [name, ds] : meta_ds_)
        H5Dclose(ds);
    H5Tclose(str_type_);
    H5Gclose(series_grp_);
    H5Fclose(file_);
}

Hdf5Writer::~Hdf5Writer() {
    close();
}

} // namespace sca
