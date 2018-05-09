#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <common_types.hpp>
#include <math.hpp>
#include <mechanism.hpp>
#include <memory/memory.hpp>
#include <util/index_into.hpp>
#include <util/optional.hpp>
#include <util/maputil.hpp>
#include <util/range.hpp>
#include <util/span.hpp>

#include <backends/fvm_types.hpp>
#include <backends/gpu/mechanism.hpp>
#include <backends/gpu/fvm.hpp>

namespace arb {
namespace gpu {

using memory::make_const_view;
using util::value_by_key;
using util::make_span;

template <typename T>
memory::device_view<T> device_view(T* ptr, std::size_t n) {
    return memory::device_view<T>(ptr, n);
}

template <typename T>
memory::const_device_view<T> device_view(const T* ptr, std::size_t n) {
    return memory::const_device_view<T>(ptr, n);
}

// The derived class (typically generated code from modcc) holds pointers that need
// to be set to point inside the shared state, or into the allocated parameter/variable
// data block.

void mechanism::instantiate(fvm_size_type id, backend::shared_state& shared, const layout& pos_data) {
    mechanism_id_ = id;
    width_ = pos_data.cv.size();

    unsigned alignment = std::max(array::alignment(), iarray::alignment());
    auto width_padded_ = math::round_up(width_, alignment);

    // Assign non-owning views onto shared state:

    mechanism_ppack_base* pp = ppack_ptr(); // From derived class instance.

    pp->vec_ci_   = shared.cv_to_cell.data();
    pp->vec_t_    = shared.time.data();
    pp->vec_t_to_ = shared.time_to.data();
    pp->vec_dt_   = shared.dt_cv.data();

    pp->vec_v_    = shared.voltage.data();
    pp->vec_i_    = shared.current_density.data();

    auto ion_state_tbl = ion_state_table();
    n_ion_ = ion_state_tbl.size();

    for (auto i: ion_state_tbl) {
        util::optional<ion_state&> oion = value_by_key(shared.ion_data, i.first);
        if (!oion) {
            throw std::logic_error("mechanism holds ion with no corresponding shared state");
        }

        ion_state_view& ion_view = *i.second;
        ion_view.current_density = oion->iX_.data();
        ion_view.reversal_potential = oion->eX_.data();
        ion_view.internal_concentration = oion->Xi_.data();
        ion_view.external_concentration = oion->Xo_.data();
    }

    event_stream_ptr_ = &shared.deliverable_events;

    // If there are no sites (is this ever meaningful?) there is nothing more to do.
    if (width_==0) {
        return;
    }

    // Allocate and initialize state and parameter vectors with default values.
    // (First sub-array of data_ is used for width_.)

    auto fields = field_table();
    std::size_t n_field = fields.size();

    data_ = array((1+n_field)*width_padded_, NAN);
    memory::copy(make_const_view(pos_data.weight), device_view(data_.data(), width_));
    pp->weight_ = data_.data();

    for (auto i: make_span(0, n_field)) {
        // Take reference to corresponding derived (generated) mechanism value pointer member.
        fvm_value_type*& field_ptr = *std::get<1>(fields[i]);
        field_ptr = data_.data()+(i+1)*width_padded_;

        if (auto opt_value = value_by_key(field_default_table(), fields[i].first)) {
            memory::fill(device_view(field_ptr, width_), *opt_value);
        }
    }

    // Allocate and initialize index vectors, viz. node_index_ and any ion indices.
    // (First sub-array of indices_ is used for node_index_.)

    indices_ = iarray((1+n_ion_)*width_padded_);

    memory::copy(make_const_view(pos_data.cv), device_view(indices_.data(), width_));
    pp->node_index_ = indices_.data();

    auto ion_index_tbl = ion_index_table();
    EXPECTS(n_ion_==ion_index_tbl.size());

    for (auto i: make_span(0, n_ion_)) {
        util::optional<ion_state&> oion = value_by_key(shared.ion_data, ion_index_tbl[i].first);
        if (!oion) {
            throw std::logic_error("mechanism holds ion with no corresponding shared state");
        }

        auto indices = util::index_into(pos_data.cv, memory::on_host(oion->node_index_));
        std::vector<index_type> mech_ion_index(indices.begin(), indices.end());

        // Take reference to derived (generated) mechanism ion index pointer.
        auto& ion_index_ptr = *ion_index_tbl[i].second;
        auto index_start = indices_.data()+(i+1)*width_padded_;
        ion_index_ptr = index_start;
        memory::copy(make_const_view(mech_ion_index), device_view(index_start, width_));
    }
}

void mechanism::set_parameter(const std::string& key, const std::vector<fvm_value_type>& values) {
    if (auto opt_ptr = value_by_key(field_table(), key)) {
        if (values.size()!=width_) {
            throw std::logic_error("internal error: mechanism parameter size mismatch");
        }

        if (width_>0) {
            // Retrieve corresponding derived (generated) mechanism value pointer member.
            value_type* field_ptr = *opt_ptr.value();
            memory::copy(make_const_view(values), device_view(field_ptr, width_));
        }
    }
    else {
        throw std::logic_error("internal error: no such mechanism parameter");
    }
}

void mechanism::set_global(const std::string& key, fvm_value_type value) {
    if (auto opt_ptr = value_by_key(global_table(), key)) {
        // Take reference to corresponding derived (generated) mechanism value member.
        value_type& global = *opt_ptr.value();
        global = value;
    }
    else {
        throw std::logic_error("internal error: no such mechanism global");
    }
}

} // namespace multicore
} // namespace arb