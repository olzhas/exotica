//
// Copyright (c) 2018, University of Edinburgh
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of  nor the names of its contributors may be used to
//    endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <exotica_core/tasks.h>

#include <exotica_core/task_initializer.h>

namespace exotica
{
Task::Task()
{
}

void Task::Initialize(const std::vector<exotica::Initializer>& inits, PlanningProblemPtr prob, TaskSpaceVector& Phi)
{
    for (const exotica::Initializer& init : inits)
    {
        TaskInitializer task(init);
        auto it = prob->GetTaskMaps().find(task.Task);
        if (it == prob->GetTaskMaps().end()) ThrowPretty("Task map '" << task.Task << "' has not been defined!");
        task_maps[task.Task] = it->second;
        tasks.push_back(it->second);
        task_initializers_.push_back(task);
    }
    num_tasks = tasks.size();
    length_phi = 0;
    length_jacobian = 0;
    Phi.map.resize(0);
    indexing.resize(tasks.size());
    for (int i = 0; i < num_tasks; ++i)
    {
        indexing[i].id = i;
        indexing[i].start = length_phi;
        indexing[i].length = tasks[i]->length;
        indexing[i].start_jacobian = length_jacobian;
        indexing[i].length_jacobian = tasks[i]->length_jacobian;

        AppendVector(Phi.map, TaskVectorEntry::reindex(tasks[i]->GetLieGroupIndices(), tasks[i]->start, indexing[i].start));
        length_phi += tasks[i]->length;
        length_jacobian += tasks[i]->length_jacobian;
    }
    Phi.SetZero(length_phi);
}

EndPoseTask::EndPoseTask()
{
}

void EndPoseTask::Initialize(const std::vector<exotica::Initializer>& inits, PlanningProblemPtr prob, TaskSpaceVector& unused)
{
    Task::Initialize(inits, prob, Phi);
    y = Phi;
    y.SetZero(length_phi);
    rho = Eigen::VectorXd::Ones(num_tasks);
    if (prob->GetFlags() & KIN_J) jacobian = Eigen::MatrixXd(length_jacobian, prob->N);
    if (prob->GetFlags() & KIN_J_DOT) hessian.setConstant(length_jacobian, Eigen::MatrixXd::Zero(prob->N, prob->N));
    S = Eigen::MatrixXd::Identity(length_jacobian, length_jacobian);
    ydiff = Eigen::VectorXd::Zero(length_jacobian);

    for (int i = 0; i < num_tasks; ++i)
    {
        TaskInitializer task(inits[i]);
        if (task.Goal.rows() == 0)
        {
            // Keep zero goal
        }
        else if (task.Goal.rows() == tasks[i]->length)
        {
            y.data.segment(indexing[i].start, indexing[i].length) = task.Goal;
        }
        else
        {
            ThrowPretty("Invalid task goal size! Expecting " << tasks[i]->length << " got " << task.Goal.rows());
        }
        if (task.Rho.rows() == 0)
        {
            rho(i) = 1.0;
        }
        else if (task.Rho.rows() == 1)
        {
            rho(i) = task.Rho(0);
        }
        else
        {
            ThrowPretty("Invalid task rho size! Expecting 1 got " << task.Rho.rows());
        }
    }
}

void EndPoseTask::UpdateS()
{
    for (const TaskIndexing& task : indexing)
    {
        for (int i = 0; i < task.length_jacobian; ++i)
        {
            S(i + task.start_jacobian, i + task.start_jacobian) = rho(task.id);
        }
        if (rho(task.id) != 0.0) tasks[task.id]->is_used = true;
    }
}

void EndPoseTask::Update(const TaskSpaceVector& big_phi, Eigen::MatrixXdRefConst big_j, HessianRefConst big_h)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi.data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
        jacobian.middleRows(task.start_jacobian, task.length_jacobian) = big_j.middleRows(tasks[task.id]->start_jacobian, tasks[task.id]->length_jacobian);
        hessian.segment(task.start, task.length) = big_h.segment(tasks[task.id]->start, tasks[task.id]->length);
    }
    ydiff = Phi - y;
}

void EndPoseTask::Update(const TaskSpaceVector& big_phi, Eigen::MatrixXdRefConst big_j)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi.data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
        jacobian.middleRows(task.start_jacobian, task.length_jacobian) = big_j.middleRows(tasks[task.id]->start_jacobian, tasks[task.id]->length_jacobian);
    }
    ydiff = Phi - y;
}

void EndPoseTask::Update(const TaskSpaceVector& big_phi)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi.data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
    }
    ydiff = Phi - y;
}

TimeIndexedTask::TimeIndexedTask()
{
}

void TimeIndexedTask::Initialize(const std::vector<exotica::Initializer>& inits, PlanningProblemPtr prob, TaskSpaceVector& Phi)
{
    Task::Initialize(inits, prob, Phi);
    Phi.SetZero(length_phi);
}

void TimeIndexedTask::UpdateS()
{
    for (int t = 0; t < T; ++t)
    {
        for (const TaskIndexing& task : indexing)
        {
            for (int i = 0; i < task.length_jacobian; ++i)
            {
                S[t](i + task.start_jacobian, i + task.start_jacobian) = rho[t](task.id);
            }
            if (rho[t](task.id) != 0.0) tasks[task.id]->is_used = true;
        }
    }
}

void TimeIndexedTask::Update(const TaskSpaceVector& big_phi, Eigen::MatrixXdRefConst big_j, HessianRefConst big_h, int t)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi[t].data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
        jacobian[t].middleRows(task.start_jacobian, task.length_jacobian) = big_j.middleRows(tasks[task.id]->start_jacobian, tasks[task.id]->length_jacobian);
        hessian[t].segment(task.start, task.length) = big_h.segment(tasks[task.id]->start, tasks[task.id]->length);
    }
    ydiff[t] = Phi[t] - y[t];
}

void TimeIndexedTask::Update(const TaskSpaceVector& big_phi, Eigen::MatrixXdRefConst big_j, int t)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi[t].data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
        jacobian[t].middleRows(task.start_jacobian, task.length_jacobian) = big_j.middleRows(tasks[task.id]->start_jacobian, tasks[task.id]->length_jacobian);
    }
    ydiff[t] = Phi[t] - y[t];
}

void TimeIndexedTask::Update(const TaskSpaceVector& big_phi, int t)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi[t].data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
    }
    ydiff[t] = Phi[t] - y[t];
}

void TimeIndexedTask::ReinitializeVariables(int _T, PlanningProblemPtr _prob, const TaskSpaceVector& phi_in)
{
    T = _T;
    Phi.assign(_T, phi_in);
    y = Phi;
    rho.assign(T, Eigen::VectorXd::Ones(num_tasks));
    if (_prob->GetFlags() & KIN_J) jacobian.assign(T, Eigen::MatrixXd(length_jacobian, _prob->N));
    if (_prob->GetFlags() & KIN_J_DOT)
    {
        Hessian Htmp;
        Htmp.setConstant(length_jacobian, Eigen::MatrixXd::Zero(_prob->N, _prob->N));
        hessian.assign(T, Htmp);
    }
    S.assign(T, Eigen::MatrixXd::Identity(length_jacobian, length_jacobian));
    ydiff.assign(T, Eigen::VectorXd::Zero(length_jacobian));

    if (num_tasks != task_initializers_.size()) ThrowPretty("Number of tasks does not match internal number of tasks!");
    for (int i = 0; i < num_tasks; ++i)
    {
        TaskInitializer& task = task_initializers_[i];
        if (task.Goal.rows() == 0)
        {
            // Keep zero goal
        }
        else if (task.Goal.rows() == tasks[i]->length * T)
        {
            for (int t = 0; t < T; ++t)
            {
                y[t].data.segment(indexing[i].start, indexing[i].length) = task.Goal.segment(t * tasks[i]->length, tasks[i]->length);
            }
        }
        else if (task.Goal.rows() == tasks[i]->length)
        {
            for (int t = 0; t < T; ++t)
            {
                y[t].data.segment(indexing[i].start, indexing[i].length) = task.Goal.segment(tasks[i]->length, tasks[i]->length);
            }
        }
        else
        {
            ThrowPretty("Invalid task goal size! Expecting " << tasks[i]->length * T << " (or 1) and got " << task.Goal.rows());
        }
        if (task.Rho.rows() == 0)
        {
            // Keep ones
        }
        else if (task.Rho.rows() == T)
        {
            for (int t = 0; t < T; ++t)
            {
                rho[t](i) = task.Rho(t);
            }
        }
        else if (task.Rho.rows() == 1)
        {
            for (int t = 0; t < T; ++t)
            {
                rho[t](i) = task.Rho(0);
            }
        }
        else
        {
            ThrowPretty("Invalid task rho size! Expecting " << T << " (or 1) and got " << task.Rho.rows());
        }
    }
}

SamplingTask::SamplingTask()
{
}

void SamplingTask::Initialize(const std::vector<exotica::Initializer>& inits, PlanningProblemPtr prob, TaskSpaceVector& unused)
{
    Task::Initialize(inits, prob, Phi);
    y = Phi;
    y.SetZero(length_phi);
    rho = Eigen::VectorXd::Ones(num_tasks);
    S = Eigen::MatrixXd::Identity(length_jacobian, length_jacobian);
    ydiff = Eigen::VectorXd::Zero(length_jacobian);

    for (int i = 0; i < num_tasks; ++i)
    {
        TaskInitializer task(inits[i]);
        if (task.Goal.rows() == 0)
        {
            // Keep zero goal
        }
        else if (task.Goal.rows() == tasks[i]->length)
        {
            y.data.segment(indexing[i].start, indexing[i].length) = task.Goal;
        }
        else
        {
            ThrowPretty("Invalid task goal size! Expecting " << tasks[i]->length << " got " << task.Goal.rows());
        }
        if (task.Rho.rows() == 0)
        {
            rho(i) = 1.0;
        }
        else if (task.Rho.rows() == 1)
        {
            rho(i) = task.Rho(0);
        }
        else
        {
            ThrowPretty("Invalid task rho size! Expecting 1 got " << task.Rho.rows());
        }
    }
}

void SamplingTask::UpdateS()
{
    for (const TaskIndexing& task : indexing)
    {
        for (int i = 0; i < task.length_jacobian; ++i)
        {
            S(i + task.start_jacobian, i + task.start_jacobian) = rho(task.id);
        }
        if (rho(task.id) != 0.0) tasks[task.id]->is_used = true;
    }
}

void SamplingTask::Update(const TaskSpaceVector& big_phi)
{
    for (const TaskIndexing& task : indexing)
    {
        Phi.data.segment(task.start, task.length) = big_phi.data.segment(tasks[task.id]->start, tasks[task.id]->length);
    }
    ydiff = Phi - y;

    for (unsigned int i = 0; i < ydiff.size(); ++i)
        if (std::abs(ydiff[i]) < tolerance) ydiff[i] = 0.0;
}
}
