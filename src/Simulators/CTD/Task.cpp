//***************************************************************************
// Copyright 2007-2017 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Faculdade de Engenharia da             *
// Universidade do Porto. For licensing terms, conditions, and further      *
// information contact lsts@fe.up.pt.                                       *
//                                                                          *
// Modified European Union Public Licence - EUPL v.1.1 Usage                *
// Alternatively, this file may be used under the terms of the Modified     *
// EUPL, Version 1.1 only (the "Licence"), appearing in the file LICENCE.md *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// https://github.com/LSTS/dune/blob/master/LICENCE.md and                  *
// http://ec.europa.eu/idabc/eupl.html.                                     *
//***************************************************************************
// Author: Pedro Calado                                                     *
//***************************************************************************

// DUNE headers.
#include <DUNE/DUNE.hpp>

using DUNE_NAMESPACES;

namespace Simulators
{
  //! CTD (Conductivity, Temperature, Depth) sensor simulator.
  //! All data is generated using a mean and standard deviation.
  //!
  //! The depth value is generated by listening to DUNE::IMC::SimulatedState
  //! and applying a standard deviation to received values.
  namespace CTD
  {
    //! %Task arguments.
    struct Arguments
    {
      //! Standard deviation of temperature measurements.
      double std_dev_temp;
      //! Mean temperature value.
      float mean_temp;
      //! Standard deviation of conductivity measurements.
      double std_dev_cond;
      //! Mean conductivity value.
      float mean_cond;
      //! Standard deviation of depth measurements.
      double std_dev_depth;
      //! Name of Pseudo-Random Number Generator to use.
      std::string prng_type;
      //! PRNG seed.
      int prng_seed;
    };

    //! %SVS simulator task.
    struct Task: public Tasks::Periodic
    {
      //! Temperature.
      IMC::Temperature m_temp;
      //! Current sound speed.
      IMC::SoundSpeed m_sspeed;
      //! Current conductivity.
      IMC::Conductivity m_cond;
      //! Current salinity.
      IMC::Salinity m_salinity;
      IMC::Depth m_depth;
      IMC::Pressure m_pressure;
      //! Last received simulated state.
      IMC::SimulatedState m_sstate;
      //! PRNG handle.
      Random::Generator* m_prng;
      //! Task arguments.
      Arguments m_args;

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Periodic(name, ctx),
        m_prng(NULL)
      {
        // Retrieve configuration values.
        param("Standard Deviation - Temperature", m_args.std_dev_temp)
        .defaultValue("1.0");

        param("Mean Value - Temperature", m_args.mean_temp)
        .defaultValue("14.0");

        param("Standard Deviation - Conductivity", m_args.std_dev_cond)
        .defaultValue("1.0");

        param("Mean Value - Conductivity", m_args.mean_cond)
        .defaultValue("4.0");

        param("Standard Deviation - Depth", m_args.std_dev_depth)
        .defaultValue("0.1");

        param("PRNG Type", m_args.prng_type)
        .description("Pseudo-Random Number Generator type. Examples: \"c_fsr256\", \"krng\"")
        .defaultValue(Random::Factory::c_default);

        param("PRNG Seed", m_args.prng_seed)
        .description("Random seed to use to random generator.")
        .defaultValue("-1");

        // Register consumers.
        bind<IMC::SimulatedState>(this);
      }

      //! Initialize resources. It requests deactivation so that this task
      //! is not active by default and will be activated only when
      //! DUNE::IMC::SimulatedState messages are received.
      void
      onResourceInitialization(void)
      {
        requestDeactivation();
      }

      //! Acquire resources. Initializes the random number generator
      void
      onResourceAcquisition(void)
      {
        //! Initialize the random number generator.
        m_prng = Random::Factory::create(m_args.prng_type, m_args.prng_seed);
      }

      //! Release resources.
      void
      onResourceRelease(void)
      {
        Memory::clear(m_prng);
      }

      //! Requests activation of the task (if not active already) and stores
      //! received state in #m_sstate.
      void
      consume(const IMC::SimulatedState* msg)
      {
        if (!isActive())
        {
          setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
          requestActivation();
        }

        m_sstate = *msg;
      }

      //! If active, computes all values using random value generators and dispatches:
      //! * @publish DUNE::IMC::Temperature
      //! * @publish DUNE::IMC::Salinity
      //! * @publish DUNE::IMC::Depth
      //! * @publish DUNE::IMC::Conductivity
      //! * @publish DUNE::IMC::SoundSpeed
      //! * @publish DUNE::IMC::Pressure
      void
      task(void)
      {
        // Return if task is not active.
        if (!isActive())
          return;

        m_temp.setTimeStamp();
        m_temp.value = m_args.mean_temp + m_prng->gaussian() * m_args.std_dev_temp;

        m_cond.setTimeStamp(m_temp.getTimeStamp());
        m_cond.value = m_args.mean_cond + m_prng->gaussian() * m_args.std_dev_cond;

        m_depth.setTimeStamp(m_temp.getTimeStamp());
        m_depth.value = std::max(m_sstate.z + m_prng->gaussian() * m_args.std_dev_depth, 0.0);

        // Compute pressure.
        m_pressure.setTimeStamp(m_temp.getTimeStamp());
        m_pressure.value = (m_depth.value * c_gravity * c_seawater_density + c_sea_level_pressure) / c_pascal_per_bar;

        m_salinity.setTimeStamp(m_temp.getTimeStamp());
        m_salinity.value = UNESCO1983::computeSalinity(m_cond.value, m_pressure.value, m_temp.value);

        m_sspeed.setTimeStamp(m_temp.getTimeStamp());
        m_sspeed.value = (m_salinity.value < 0.0) ? -1.0 : UNESCO1983::computeSoundSpeed(m_salinity.value, m_pressure.value, m_temp.value);

        dispatch(m_temp, DF_KEEP_TIME);
        dispatch(m_cond, DF_KEEP_TIME);
        dispatch(m_depth, DF_KEEP_TIME);
        dispatch(m_pressure, DF_KEEP_TIME);
        dispatch(m_salinity, DF_KEEP_TIME);
        dispatch(m_sspeed, DF_KEEP_TIME);
      }
    };
  }
}

DUNE_TASK