/* -------------------------------------------------------------------------- *
 * OpenSim Moco: example2DWalking.cpp                                         *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017-19 Stanford University and the Authors                  *
 *                                                                            *
 * Author(s): Antoine Falisse                                                 *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

/// This example features two different optimal control problems:
///  - The first problem is a tracking simulation of walking.
///  - The second problem is a predictive simulation of walking.
///
/// The code is inspired from Falisse A, Serrancoli G, Dembia C, Gillis J,
/// De Groote F: Algorithmic differentiation improves the computational
/// efficiency of OpenSim-based trajectory optimization of human movement.
/// PLOS One, 2019.
///
/// Model
/// -----
/// The model described in the file '2D_gait.osim' included in this file is a
/// modified version of the 'gait10dof18musc.osim' available within OpenSim. We
/// replaced the moving knee flexion axis by a fixed flexion axis, replaced the
/// Millard2012EquilibriumMuscles by DeGrooteFregly2016Muscles, and added
/// SmoothSphereHalfSpaceForces (two contact spheres per foot) to model the
/// contact interactions between the feet and the ground. We also added
/// polynomial approximations of muscle path lengths. We optimized the
/// polynomial coefficients using custom MATLAB code to fit muscle-tendon
/// lengths and moment arms (maximal root mean square deviation: 3 mm) obtained
/// from OpenSim using a wide range of coordinate values.
///
/// Data
/// ----
/// The coordinate data included in the 'referenceCoordinates.sto' comes from
/// predictive simulations generated in Falisse et al. 2019.

#include <Moco/osimMoco.h>

using namespace OpenSim;

// Set a coordinate tracking problem where the goal is to minimize the
// difference between provided and simulated coordinate values and speeds
// as well as to minimize an effort cost (squared controls). The provided data
// represents half a gait cycle. Endpoint constraints enforce periodicity of
// the coordinate values (except for pelvis tx) and speeds, coordinate
// actuator controls, and muscle activations. The tracking problem is solved
// using polynomial approximations of muscle path lengths if true is passed as
// an input argument, whereas geometry paths are used with the argument false.
MocoSolution gaitTracking(const bool& setPathLengthApproximation) {

    using SimTK::Pi;

    MocoTrack track;
    track.setName("gaitTracking");

    // Define the optimal control problem.
    // ===================================
    ModelProcessor modelprocessor =
            ModelProcessor("2D_gait.osim") |
            ModOpSetPathLengthApproximation(setPathLengthApproximation);
    track.setModel(modelprocessor);
    track.setStatesReference(
            TableProcessor("referenceCoordinates.sto") | TabOpLowPassFilter(6));
    track.set_states_global_tracking_weight(10.0);
    track.set_allow_unused_references(true);
    track.set_track_reference_position_derivatives(true);
    track.set_apply_tracked_states_to_guess(true);
    track.set_initial_time(0.0);
    track.set_final_time(0.47008941);
    MocoStudy moco = track.initialize();
    MocoProblem& problem = moco.updProblem();

    // Goals.
    // =====
    // Symmetry.
    auto* symmetryGoal = problem.addGoal<MocoPeriodicityGoal>("symmetryGoal");
    Model model = modelprocessor.process();
    model.initSystem();
    // Symmetric coordinate values (except for pelvis_tx) and speeds.
    for (const auto& coord : model.getComponentList<Coordinate>()) {
        if (endsWith(coord.getName(), "_r")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    std::regex_replace(coord.getStateVariableNames()[0],
                            std::regex("_r"), "_l")});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    std::regex_replace(coord.getStateVariableNames()[1],
                            std::regex("_r"), "_l")});
        }
        if (endsWith(coord.getName(), "_l")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    std::regex_replace(coord.getStateVariableNames()[0],
                            std::regex("_l"), "_r")});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    std::regex_replace(coord.getStateVariableNames()[1],
                            std::regex("_l"), "_r")});
        }
        if (!endsWith(coord.getName(), "_l") &&
                !endsWith(coord.getName(), "_r") &&
                !endsWith(coord.getName(), "_tx")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    coord.getStateVariableNames()[0]});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    coord.getStateVariableNames()[1]});
        }
    }
    symmetryGoal->addStatePair({"/jointset/groundPelvis/pelvis_tx/speed"});
    // Symmetric coordinate actuator controls.
    symmetryGoal->addControlPair({"/lumbarAct"});
    // Symmetric muscle activations.
    for (const auto& muscle : model.getComponentList<Muscle>()) {
        if (endsWith(muscle.getName(), "_r")) {
            symmetryGoal->addStatePair({muscle.getStateVariableNames()[0],
                    std::regex_replace(muscle.getStateVariableNames()[0],
                            std::regex("_r"), "_l")});
        }
        if (endsWith(muscle.getName(), "_l")) {
            symmetryGoal->addStatePair({muscle.getStateVariableNames()[0],
                    std::regex_replace(muscle.getStateVariableNames()[0],
                            std::regex("_l"), "_r")});
        }
    }
    // Effort. Get a reference to the MocoControlGoal that is added to every
    // MocoTrack problem by default.
    MocoControlGoal& effort =
            dynamic_cast<MocoControlGoal&>(problem.updGoal("control_effort"));
    effort.setWeight(10);

    // Bounds.
    // =======
    problem.setStateInfo("/jointset/groundPelvis/pelvis_tilt/value",
            {-20 * Pi / 180, -10 * Pi / 180});
    problem.setStateInfo("/jointset/groundPelvis/pelvis_tx/value", {0, 1});
    problem.setStateInfo(
            "/jointset/groundPelvis/pelvis_ty/value", {0.75, 1.25});
    problem.setStateInfo("/jointset/hip_l/hip_flexion_l/value",
            {-10 * Pi / 180, 60 * Pi / 180});
    problem.setStateInfo("/jointset/hip_r/hip_flexion_r/value",
            {-10 * Pi / 180, 60 * Pi / 180});
    problem.setStateInfo(
            "/jointset/knee_l/knee_angle_l/value", {-50 * Pi / 180, 0});
    problem.setStateInfo(
            "/jointset/knee_r/knee_angle_r/value", {-50 * Pi / 180, 0});
    problem.setStateInfo("/jointset/ankle_l/ankle_angle_l/value",
            {-15 * Pi / 180, 25 * Pi / 180});
    problem.setStateInfo("/jointset/ankle_r/ankle_angle_r/value",
            {-15 * Pi / 180, 25 * Pi / 180});
    problem.setStateInfo("/jointset/lumbar/lumbar/value", {0, 20 * Pi / 180});

    // Configure the solver.
    // =====================
    MocoCasADiSolver& solver = moco.updSolver<MocoCasADiSolver>();
    solver.set_num_mesh_intervals(50);
    solver.set_verbosity(2);
    solver.set_optim_solver("ipopt");
    solver.set_optim_convergence_tolerance(1e-4);
    solver.set_optim_constraint_tolerance(1e-4);
    solver.set_optim_max_iterations(1000);

    // Solve problem.
    // ==============
    MocoSolution solution = moco.solve();
    auto full = createPeriodicTrajectory(solution);
    full.write("gaitTracking_solution_fullcycle.sto");

    // moco.visualize(solution);

    return solution;
}

// Set a gait prediction problem where the goal is to minimize effort (squared
// controls) over distance traveled while enforcing symmetry of the walking
// cycle and a prescribed average gait speed through endpoint constraints. The
// solution of the coordinate tracking problem is passed as an input argument
// and used as an initial guess for the prediction. The predictive problem is
// solved using polynomial approximations of muscle path lengths if true is
// passed as an input argument, whereas geometry paths are used with the
// argument false. Polynomial approximations should improve the computation
// speeds by about 25% for this problem.
void gaitPrediction(const MocoSolution& gaitTrackingSolution,
        const bool& setPathLengthApproximation) {

    using SimTK::Pi;

    MocoStudy moco;
    moco.setName("gaitPrediction");

    // Define the optimal control problem.
    // ===================================
    MocoProblem& problem = moco.updProblem();
    ModelProcessor modelprocessor =
            ModelProcessor("2D_gait.osim") |
            ModOpSetPathLengthApproximation(setPathLengthApproximation);
    problem.setModelProcessor(modelprocessor);

    // Goals.
    // =====
    // Symmetry.
    auto* symmetryGoal = problem.addGoal<MocoPeriodicityGoal>("symmetryGoal");
    Model model = modelprocessor.process();
    model.initSystem();
    // Symmetric coordinate values (except for pelvis_tx) and speeds.
    for (const auto& coord : model.getComponentList<Coordinate>()) {
        if (endsWith(coord.getName(), "_r")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    std::regex_replace(coord.getStateVariableNames()[0],
                            std::regex("_r"), "_l")});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    std::regex_replace(coord.getStateVariableNames()[1],
                            std::regex("_r"), "_l")});
        }
        if (endsWith(coord.getName(), "_l")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    std::regex_replace(coord.getStateVariableNames()[0],
                            std::regex("_l"), "_r")});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    std::regex_replace(coord.getStateVariableNames()[1],
                            std::regex("_l"), "_r")});
        }
        if (!endsWith(coord.getName(), "_l") &&
                !endsWith(coord.getName(), "_r") &&
                !endsWith(coord.getName(), "_tx")) {
            symmetryGoal->addStatePair({coord.getStateVariableNames()[0],
                    coord.getStateVariableNames()[0]});
            symmetryGoal->addStatePair({coord.getStateVariableNames()[1],
                    coord.getStateVariableNames()[1]});
        }
    }
    symmetryGoal->addStatePair({"/jointset/groundPelvis/pelvis_tx/speed"});
    // Symmetric coordinate actuator controls.
    symmetryGoal->addControlPair({"/lumbarAct"});
    // Symmetric muscle activations.
    for (const auto& muscle : model.getComponentList<Muscle>()) {
        if (endsWith(muscle.getName(), "_r")) {
            symmetryGoal->addStatePair({muscle.getStateVariableNames()[0],
                    std::regex_replace(muscle.getStateVariableNames()[0],
                            std::regex("_r"), "_l")});
        }
        if (endsWith(muscle.getName(), "_l")) {
            symmetryGoal->addStatePair({muscle.getStateVariableNames()[0],
                    std::regex_replace(muscle.getStateVariableNames()[0],
                            std::regex("_l"), "_r")});
        }
    }
    // Prescribed average gait speed.
    auto* speedGoal = problem.addGoal<MocoAverageSpeedGoal>("speed");
    speedGoal->set_desired_average_speed(1.2);
    // Effort over distance.
    auto* effortGoal = problem.addGoal<MocoControlGoal>("effort", 10);
    effortGoal->setExponent(3);
    effortGoal->setDivideByDisplacement(true);

    // Bounds.
    // =======
    problem.setTimeBounds(0, {0.4, 0.6});
    problem.setStateInfo("/jointset/groundPelvis/pelvis_tilt/value",
            {-20 * Pi / 180, -10 * Pi / 180});
    problem.setStateInfo("/jointset/groundPelvis/pelvis_tx/value", {0, 1});
    problem.setStateInfo(
            "/jointset/groundPelvis/pelvis_ty/value", {0.75, 1.25});
    problem.setStateInfo("/jointset/hip_l/hip_flexion_l/value",
            {-10 * Pi / 180, 60 * Pi / 180});
    problem.setStateInfo("/jointset/hip_r/hip_flexion_r/value",
            {-10 * Pi / 180, 60 * Pi / 180});
    problem.setStateInfo(
            "/jointset/knee_l/knee_angle_l/value", {-50 * Pi / 180, 0});
    problem.setStateInfo(
            "/jointset/knee_r/knee_angle_r/value", {-50 * Pi / 180, 0});
    problem.setStateInfo("/jointset/ankle_l/ankle_angle_l/value",
            {-15 * Pi / 180, 25 * Pi / 180});
    problem.setStateInfo("/jointset/ankle_r/ankle_angle_r/value",
            {-15 * Pi / 180, 25 * Pi / 180});
    problem.setStateInfo("/jointset/lumbar/lumbar/value", {0, 20 * Pi / 180});

    // Configure the solver.
    // =====================
    auto& solver = moco.initCasADiSolver();
    solver.set_num_mesh_intervals(50);
    solver.set_verbosity(2);
    solver.set_optim_solver("ipopt");
    solver.set_optim_convergence_tolerance(1e-4);
    solver.set_optim_constraint_tolerance(1e-4);
    solver.set_optim_max_iterations(1000);
    // Use the solution from the tracking simulation as initial guess.
    solver.setGuess(gaitTrackingSolution);

    // Solve problem.
    // ==============
    MocoSolution solution = moco.solve();
    auto full = createPeriodicTrajectory(solution);
    full.write("gaitPrediction_solution_fullcycle.sto");

    // Extract ground reaction forces.
    // ===============================
    std::vector<std::string> contactSpheres_r;
    std::vector<std::string> contactSpheres_l;
    contactSpheres_r.push_back("contactSphereHeel_r");
    contactSpheres_r.push_back("contactSphereFront_r");
    contactSpheres_l.push_back("contactSphereHeel_l");
    contactSpheres_l.push_back("contactSphereFront_l");
    TimeSeriesTable externalForcesTableFlat = createExternalLoadsTableForGait(
            model, full, contactSpheres_r, contactSpheres_l);
    writeTableToFile(externalForcesTableFlat,
            "gaitPrediction_solutionGRF_fullcycle.sto");

    moco.visualize(full);
}

int main() {
    try {
        // Use polynomial approximations of muscle path lengths (set false to
        // use GeometryPath).
        const MocoSolution gaitTrackingSolution = gaitTracking(false);
        gaitPrediction(gaitTrackingSolution, false);
    } catch (const std::exception& e) { std::cout << e.what() << std::endl; }
    return EXIT_SUCCESS;
}