#ifndef CASM_MonteDriver_HH
#define CASM_MonteDriver_HH

#include <string>
#include <chrono>

#include "casm/monte_carlo/MonteIO.hh"
#include "casm/monte_carlo/MonteCarlo.hh"
#include "casm/monte_carlo/MonteSettings.hh"

namespace CASM {

  /**
   * MonteDriver consists of a specialized MonteCarlo object and a list of conditions
   * to equilibrate at. The first condition in the list requires a starting configuration
   * (read from the setting), while subsequent conditions are calculated using the
   * final state of the previous condition.
   *
   * The different kinds of drive modes the user can specify are:
   * SINGLE:        Just equilibrate the initial settings
   * INCREMENTAL:   Given a delta in condition values, increment the conditions by the delta after each point
   */

  template<typename RunType>
  class MonteDriver {

  public:
    typedef typename RunType::CondType CondType;
    typedef typename RunType::SettingsType SettingsType;

    /// \brief Constructor via MonteSettings
    MonteDriver(PrimClex &primclex, const SettingsType &settings, std::ostream &_sout = std::cout);

    /// \brief Run everything requested by the MonteSettings
    void run();

  private:

    /// run in debug mode?
    bool debug() const {
      return m_debug;
    }

    ///Return the appropriate std::vector of conditions to visit based from settings. Use for construction.
    std::vector<CondType> make_conditions_list(const PrimClex &primclex, const SettingsType &settings);

    ///Converge the MonteCarlo as it currently stands
    void single_run();

    ///Check for existing calculations to find starting conditions
    Index _find_starting_conditions() const;


    ///Copy of initial settings given at construction. Will expand to have MonteCarlo states dumped into it.
    SettingsType m_settings;

    ///Specifies how to build the conditions list from the settings
    const Monte::DRIVE_MODE m_drive_mode;

    ///Specialized Monte Carlo object to use throughout
    RunType m_mc;

    ///List of specialized conditions to visit in sequential order. Does not include initial conditions.
    const std::vector<CondType> m_conditions_list;

    /// run in debug mode?
    bool m_debug;

    /// target for log messages
    std::ostream &sout;

  };


  /// Perform a single monte carlo step, incrementing monte_run.m_Nstep and monte_run.Npass as it goes
  template<typename RunType>
  void monte_carlo_step(RunType &monte_run);


  template<typename RunType>
  MonteDriver<RunType>::MonteDriver(PrimClex &primclex, const SettingsType &settings, std::ostream &_sout):
    m_settings(settings),
    m_drive_mode(m_settings.drive_mode()),
    m_mc(primclex, m_settings),
    m_conditions_list(make_conditions_list(primclex, m_settings)),
    m_debug(m_settings.debug()),
    sout(_sout) {

  }

  /// \brief Run calculations for all conditions, outputting data as you finish each one
  ///
  /// - Assumes existing "output_dir/conditions.i/final_state.json" files indicates finished
  ///   calculations that are already included in the results summary "output_dir/results.X",
  ///   and that no other results are written to the results summary.
  /// - If there are existing results, uses "output_dir/conditions.i/final_state.json" as
  ///   the initial state for the next run
  template<typename RunType>
  void MonteDriver<RunType>::run() {

    if(debug()) {
      sout << "Checking for existing calculations..." << std::endl;
    }
    MonteCarloDirectoryStructure dir(m_settings.output_directory());

    if(!m_settings.write_json() && !m_settings.write_csv()) {
      throw std::runtime_error(
        std::string("No valid monte carlo output format.\n") +
        "  Expected [\"data\"][\"storage\"][\"output_format\"] to contain a string or array of strings.\n" +
        "  Valid options are 'csv' or 'json'.");
    }

    // Skip any conditions that have already been calculated and saved
    Index start_i = _find_starting_conditions();

    // check if we'll be repeating any calculations that already have files written
    std::vector<Index> repeats;
    for(Index i = start_i; i < m_conditions_list.size(); ++i) {
      if(fs::exists(dir.conditions_dir(i))) {
        repeats.push_back(i);
      }
    }

    if(start_i == m_conditions_list.size()) {
      sout << "Calculations already complete." << std::endl;
      return;
    }

    // if existing calculations
    if(start_i > 0 || repeats.size() > 0) {

      sout << "Found existing calculations. Will begin with condition " << start_i << ".\n" << std::endl;

      if(repeats.size()) {
        jsonParser json;
        to_json(repeats, json);
        sout << "Will overwrite existing results for condition(s): " << json << "\n" << std::endl;
      }
    }

    // if starting from initial condition
    if(start_i == 0) {
      // perform any requested explicit equilibration passes
      if(m_settings.is_equilibration_passes_first_run()) {
        auto equil_passes = m_settings.equilibration_passes_first_run();
        sout << "Begin " << equil_passes << " equilibration passes..." << std::endl;

        MonteCounter equil_counter(m_settings, m_mc.steps_per_pass());
        while(equil_counter.pass() != equil_passes) {
          monte_carlo_step(m_mc);
          equil_counter++;
        }

        sout << "  DONE" << std::endl;
      }
    }
    else {
      // read end state of previous condition
      ConfigDoF configdof = m_mc.configdof();
      from_json(configdof, jsonParser(dir.final_state_json(start_i - 1)));
      m_mc.reset(configdof);
    }

    // Run for all conditions, outputting data as you finish each one
    for(Index i = start_i; i < m_conditions_list.size(); i++) {

      m_mc.set_conditions(m_conditions_list[i]);

      single_run();
      sout << "Writing output files..." << std::endl;
      m_mc.write_results(i);
      jsonParser json;
      to_json(m_mc.configdof(), json).write(dir.final_state_json(i));
      sout << "  DONE" << std::endl << std::endl;

    }

    return;
  }

  /// \brief Checks existing files to determine where to restart a path
  ///
  /// - Will overwrite or cause to overwrite files in cases where the
  ///   final state or results summary do not exist for some conditions
  ///
  template<typename RunType>
  Index MonteDriver<RunType>::_find_starting_conditions() const {

    MonteCarloDirectoryStructure dir(m_settings.output_directory());
    Index start_i = 0;
    Index start_max = m_conditions_list.size() - 1;
    Index start_json = m_settings.write_json() ? 0 : start_max;
    Index start_csv = m_settings.write_csv() ? 0 : start_max;
    jsonParser json_results;
    fs::ifstream csv_results;
    std::string str;
    std::stringstream ss;

    // can start with condition i+1 if results (i) and final_state.json (i) exist

    // check JSON files
    if(m_settings.write_json() && fs::exists(dir.results_json())) {

      json_results.read(dir.results_json());

      // can start with i+1 if results[i] and final_state.json (i) exist
      while(json_results.begin()->size() > start_json && fs::exists(dir.final_state_json(start_i)) && start_i < start_max) {
        ++start_json;
      }

      start_max = start_json;
    }

    // check CSV files
    if(m_settings.write_csv() && fs::exists(dir.results_csv())) {

      csv_results.open(dir.results_csv());

      // read header
      std::getline(csv_results, str);
      ss << str << "\n";

      // can start with i+1 if results[i] and final_state.json (i) exist
      while(!csv_results.eof() && fs::exists(dir.final_state_json(start_csv)) && start_i < start_max) {
        ++start_csv;
        std::getline(csv_results, str);
        ss << str << "\n";
      }

      start_max = start_csv;
    }
    csv_results.close();

    // use minimum of allowed starting conditions, in case a difference is found
    start_i = std::min(start_json, start_csv);


    // update results summary files to remove any conditions that must be re-calculated

    // for JSON
    if(m_settings.write_json() && fs::exists(dir.results_json())) {

      // if results size > start_i, must fix results by removing results that will be re-run
      jsonParser finished_results;
      for(auto it = json_results.begin(); it != json_results.end(); ++it) {
        jsonParser &ref = finished_results[it.name()].put_array();
        for(Index i = 0; i < start_i; ++i) {
          ref.push_back((*it)[i]);
        }
      }
      finished_results.write(dir.results_json());
    }

    // for csv
    if(m_settings.write_csv() && fs::exists(dir.results_csv())) {
      fs::ofstream out(dir.results_csv());
      out << ss.rdbuf();
      out.close();
    }

    return start_i;
  }

  template<typename RunType>
  void MonteDriver<RunType>::single_run() {

    // perform any requested explicit equilibration passes
    if(m_settings.is_equilibration_passes_each_run()) {

      auto equil_passes = m_settings.equilibration_passes_each_run();
      sout << "Begin " << equil_passes << " equilibration passes..." << std::endl;

      MonteCounter equil_counter(m_settings, m_mc.steps_per_pass());
      while(equil_counter.pass() != equil_passes) {
        monte_carlo_step(m_mc);
        equil_counter++;
      }

      sout << "  DONE" << std::endl;
    }


    // timing info:
    using namespace std::chrono;
    steady_clock::time_point start_time, curr_time;
    start_time = steady_clock::now();

    m_mc.print_run_start_info();

    MonteCounter run_counter(m_settings, m_mc.steps_per_pass());


    while(true) {

      if(debug()) {
        sout << "\n-----------------------------------------\n"
             << "Pass: " << run_counter.pass() << "  "
             << "Step: " << run_counter.step() << "  "
             << "Samples: " << run_counter.samples() << std::endl;
      }

      if(m_mc.must_converge()) {

        if(!run_counter.minimums_met()) {

          // keep going, but check for conflicts with maximums
          if(run_counter.maximums_met()) {
            throw std::runtime_error(
              std::string("Error in 'MonteDriver<RunType>::single_run()'\n") +
              "  Conflicting input: Minimum number of passes, steps, or samples not met,\n" +
              "  but maximum number of passes, steps, or samples are met.");
          }
        }
        else if(m_mc.check_convergence_time() && m_mc.is_converged()) {

          // stop
          break;
        }
        else if(run_counter.maximums_met()) {

          // stop
          break;
        }
      }
      else if(run_counter.is_complete()) {
        // stop
        break;
      }

      monte_carlo_step(m_mc);

      run_counter++;

      if(run_counter.sample_time()) {
        if(debug()) {
          sout << "** Sample data **" << std::endl;
        }
        m_mc.sample_data(run_counter.pass(), run_counter.step());
        run_counter.increment_samples();
      }

      //run_counter.debugprint(sout);

    }

    //sout << "--- Out of loop ---" << std::endl;
    //run_counter.debugprint(sout);

    // timing info:
    curr_time = steady_clock::now();
    float s = duration_cast<duration<float> >(curr_time - start_time).count();
    sout << "Run time: " << s << " (s),  " << s / run_counter.pass() << " (s/pass),  " << s / (run_counter.pass()*run_counter.steps_per_pass() + run_counter.step()) << "(s/step)" << std::endl;
    //start_time = curr_time;

    return;
  }

  /**
   * Reads from the settings and constructs an appropriate
   * std::vector of conditions for MonteDriver to visit.
   *
   * Options are:
   * * Single: only visit initial conditions (Returns empty array)
   * * Custom: Provide explicit list of conditions to visit
   * * Incremental: specify initial conditions, final conditions
   * and regular intervals
   */

  template<typename RunType>
  std::vector<typename MonteDriver<RunType>::CondType>
  MonteDriver<RunType>::make_conditions_list(const PrimClex &primclex, const SettingsType &settings) {

    std::vector<CondType> conditions_list;

    switch(m_drive_mode) {

    case Monte::DRIVE_MODE::SINGLE: {
      conditions_list.push_back(settings.initial_conditions());
      break;
    }

    case Monte::DRIVE_MODE::INCREMENTAL: {

      CondType init_cond(settings.initial_conditions());
      CondType final_cond(settings.final_conditions());
      CondType cond_increment(settings.incremental_conditions());

      conditions_list.push_back(init_cond);

      CondType incrementing_cond = init_cond;
      incrementing_cond.increment_by(cond_increment);

      int num_increments = (final_cond - init_cond) / cond_increment;

      for(int i = 0; i < num_increments; i++) {
        conditions_list.push_back(incrementing_cond);
        incrementing_cond.increment_by(cond_increment);
      }

      if(conditions_list.size() == 1) {
        std::cerr << "WARNING in MonteDriver::initialize" << std::endl;
        std::cerr << "You specified incremental drive mode, but the specified increment resulted in single drive mode behavior." << std::endl;
        std::cerr << "Only the initial condition will be calculated! (Is your increment too big or are you incrementing too many things?)" << std::endl;
      }

      break;
    }

    default: {
      throw std::runtime_error(
        std::string("ERROR in MonteDriver::initialize\n") +
        "  An invalid drive mode was given.");

    }

    }

    return conditions_list;
  }

  template<typename RunType>
  void monte_carlo_step(RunType &monte_run) {

    typedef typename RunType::EventType EventType;

    const EventType &event = monte_run.propose();

    if(monte_run.check(event)) {
      monte_run.accept(event);
    }

    else {
      monte_run.reject(event);
    }

    return;
  }

}

#endif
