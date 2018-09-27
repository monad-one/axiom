#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AudioBackend.h"

namespace AxiomBackend {

    template<class T>
    struct PersistentParameter {
        uint64_t id;
        size_t portalIndex;
        T **value;
        std::string name;

        PersistentParameter(uint64_t id, size_t portalIndex, T **value, std::string name)
            : id(id), portalIndex(portalIndex), value(value), name(std::move(name)) {}
    };

    using NumParameter = PersistentParameter<AxiomBackend::NumValue>;
    using MidiParameter = PersistentParameter<AxiomBackend::MidiValue>;

    template<class T>
    class PersistentParameters {
    public:
        const std::vector<std::optional<PersistentParameter<T>>> &parameters() const { return _parameters; }

        const std::unordered_map<size_t, size_t> &portalParameterMap() const { return _portalParameterMap; }

        size_t size() const { return _parameters.size(); }

        const std::optional<PersistentParameter<T>> &operator[](size_t index) const { return _parameters[index]; }

        void setParameters(std::vector<PersistentParameter<T>> newParameters) {
            // build a map of the portal ID to parameter index
            std::unordered_map<uint64_t, size_t> parameterIndexMap;
            for (size_t parameterIndex = 0; parameterIndex < _parameters.size(); parameterIndex++) {
                auto &parameter = _parameters[parameterIndex];
                if (parameter) {
                    parameterIndexMap.emplace(parameter->id, parameterIndex);
                }
            }

            _parameters.clear();
            std::vector<PersistentParameter<T>> queuedParameters;

            for (auto &newParameter : newParameters) {
                // if the parameter had a previous index, insert it there, else queue it to be inserted later
                auto previousParameterIndex = parameterIndexMap.find(newParameter.id);
                if (previousParameterIndex != parameterIndexMap.end()) {
                    insertParameter(previousParameterIndex->second, std::move(newParameter));
                } else {
                    queuedParameters.push_back(std::move(newParameter));
                }
            }

            // go through and insert the remaining parameters
            for (auto &queuedParam : queuedParameters) {
                pushParameter(std::move(queuedParam));
            }
        }

    private:
        std::vector<std::optional<PersistentParameter<T>>> _parameters;
        std::unordered_map<size_t, size_t> _portalParameterMap;

        void insertParameter(size_t insertIndex, PersistentParameter<T> param) {
            while (_parameters.size() <= insertIndex) _parameters.emplace_back(std::nullopt);
            _parameters[insertIndex] = std::move(param);
            _portalParameterMap.emplace(param.portalIndex, insertIndex);
        }

        void pushParameter(PersistentParameter<T> param) {
            // loop until we find an available index
            size_t nextIndex = 0;
            while (nextIndex < _parameters.size() && _parameters[nextIndex]) {
                nextIndex++;
            }

            insertParameter(nextIndex, std::move(param));
        }
    };

    using NumParameters = PersistentParameters<AxiomBackend::NumValue>;
    using MidiParameters = PersistentParameters<AxiomBackend::MidiValue>;
}
