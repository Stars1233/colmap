// Copyright (c), ETH Zurich and UNC Chapel Hill.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "colmap/sensor/rig.h"

namespace colmap {

void Rig::AddRefSensor(sensor_t ref_sensor_id) {
  THROW_CHECK(ref_sensor_id_ == kInvalidSensorId)
      << "Reference sensor already set";
  ref_sensor_id_ = ref_sensor_id;
}

void Rig::AddSensor(sensor_t sensor_id,
                    const std::optional<Rigid3d>& sensor_from_rig) {
  THROW_CHECK_GE(NumSensors(), 1)
      << "The reference sensor needs to be added first before other sensors.";
  THROW_CHECK(!HasSensor(sensor_id))
      << "Sensor (" << sensor_id.type << ", " << sensor_id.id
      << ") is inserted twice into the rig";
  sensors_from_rig_.emplace(sensor_id, sensor_from_rig);
}

std::ostream& operator<<(std::ostream& stream, const Rig& rig) {
  const std::string rig_id_str =
      rig.RigId() != kInvalidRigId ? std::to_string(rig.RigId()) : "Invalid";
  stream << "Rig(rig_id=" << rig_id_str << ", ref_sensor_id=("
         << rig.RefSensorId().type << ", " << rig.RefSensorId().id
         << "), sensors=[";
  for (auto it = rig.Sensors().begin(); it != rig.Sensors().end();) {
    stream << "(" << it->first.type << ", " << it->first.id << ")";
    if (++it != rig.Sensors().end()) {
      stream << ", ";
    }
  }
  stream << "])";
  return stream;
}

}  // namespace colmap
