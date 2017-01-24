#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

/// What is being tested:
///   the command line executable

/// What is being used to test it:
#include <boost/filesystem.hpp>

#include "Common.hh"

using namespace CASM;

struct Checks {

  Checks(std::string _type,
         std::string _no_proj_command,
         std::string _help_command) :
    type(_type),
    no_proj_command(_no_proj_command),
    help_command(_help_command) {}

  std::string type;
  std::string no_proj_command;
  std::string help_command;
};

BOOST_AUTO_TEST_SUITE(AppTest)

BOOST_AUTO_TEST_CASE(ProjectCommands) {

  Popen p;
  Log &log = default_log();

  // checks for what happens when running casm from a non-project directory
  std::vector<Checks> command = {
    Checks("composition", "-d", "-h"),
    Checks("sym", "", "-h"),
    Checks("bset", "", "-h"),
    Checks("enum", "", "-h"),
    Checks("select", "", "-h"),
    Checks("query", "", "-h")
  };

  // check help doesn't need to be in a project
  p.popen("casm init -h");
  BOOST_CHECK_MESSAGE(p.exit_code() == 0, p.gets());

  for(auto it = command.begin(); it != command.end(); ++it) {
    p.popen("casm " + it->type + " " + it->no_proj_command);
    BOOST_CHECK_MESSAGE(p.exit_code() == 3, p.gets());

    p.popen("casm " + it->type + " " + it->help_command);
    BOOST_CHECK_MESSAGE(p.exit_code() == 0, p.gets());
  }

  // checks of 'casm X' commands for several projects
  std::vector<std::unique_ptr<test::Proj> > proj;
  proj.push_back(notstd::make_unique<test::FCCTernaryProj>());
  proj.push_back(notstd::make_unique<test::ZrOProj>());

  for(auto proj_it = proj.begin(); proj_it != proj.end(); ++proj_it) {
    log.custom<Log::standard>("Test project: " + (*proj_it)->title);
    log << "root: " << (*proj_it)->dir << std::endl;

    log << "testing 'casm init'" << std::endl;
    (*proj_it)->check_init();

    log << "testing 'casm sym'" << std::endl;
    (*proj_it)->check_symmetry();

    log << "testing 'casm composition'" << std::endl;
    (*proj_it)->check_composition();

    log << "testing 'casm bset'" << std::endl;
    (*proj_it)->check_bset();

    log << "testing 'casm enum'" << std::endl;
    (*proj_it)->check_enum();

    log << "testing 'casm select'" << std::endl;
    (*proj_it)->check_select();

    log << "testing 'casm query'" << std::endl;
    (*proj_it)->check_query();

    log << std::endl;

  }

}

BOOST_AUTO_TEST_SUITE_END()
