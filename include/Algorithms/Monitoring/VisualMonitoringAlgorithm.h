/**
 *  @file   PandoraPFANew/include/Algorithms/Monitoring/VisualMonitoringAlgorithm.h
 * 
 *  @brief  Header file for the visual monitoring algorithm class
 * 
 *  $Log: $
 */
#ifndef VISUAL_MONITORING_ALGORITHM_H
#define VISUAL_MONITORING_ALGORITHM_H 1

#include "Algorithms/Algorithm.h"

/**
 *  @brief VisualMonitoringAlgorithm class
 */
class VisualMonitoringAlgorithm : public pandora::Algorithm
{
private:
public:
    /**
     *  @brief  Factory class for instantiating algorithm
     */
    class Factory : public pandora::AlgorithmFactory
    {
    public:
        Algorithm *CreateAlgorithm() const;
    };

private:
    StatusCode Run();
    StatusCode ReadSettings(const TiXmlHandle xmlHandle);

    /**
     *  @brief  Visualize a specified ordered calo hit list
     * 
     *  @param  listName the list name
     */
    void VisualizeMCParticleList() const;

    /**
     *  @brief  Visualize a specified ordered calo hit list
     * 
     *  @param  listName the list name
     */
    void VisualizeOrderedCaloHitList(const std::string &listName) const;

    /**
     *  @brief  Visualize a specified ordered calo hit list
     * 
     *  @param  listName the list name
     */
    void VisualizeTrackList(const std::string &listName) const;

    /**
     *  @brief  Visualize a specified ordered calo hit list
     * 
     *  @param  listName the list name
     */
    void VisualizeClusterList(const std::string &listName) const;

    /**
     *  @brief  Visualize a specified ordered calo hit list
     * 
     *  @param  listName the list name
     */
    void VisualizeParticleFlowList() const;

    typedef std::map<int, float> PdgCodeToEnergyMap;

    bool                    m_showMCParticles;          ///< Whether to show MC particles

    bool                    m_showCurrentCaloHits;      ///< Whether to show current ordered calohitlist
    pandora::StringVector   m_caloHitListNames;         ///< Names of calo hit lists to show

    bool                    m_showCurrentTracks;        ///< Whether to show current ordered calohitlist
    pandora::StringVector   m_trackListNames;           ///< Names of calo hit lists to show

    bool                    m_showCurrentClusters;      ///< Whether to show current ordered calohitlist
    pandora::StringVector   m_clusterListNames;         ///< Names of calo hit lists to show

    bool                    m_showCurrentPfos;          ///< Whether to show current particle flow object list
    bool                    m_showOnlyAvailable;        ///< Whether to show only available  (i.e. non-clustered) calohits and tracks
    bool                    m_displayEvent;             ///< Whether to display the event

    pandora::StringVector   m_suppressMCParticles;      ///< List of PDG numbers and energies for MC particles to be suppressed (e.g. " 22:0.1 2112:1.0 ")
    PdgCodeToEnergyMap      m_particleSuppressionMap;   ///< Map from pdg-codes to energy for suppression of particles types below specific energies
};

//------------------------------------------------------------------------------------------------------------------------------------------

inline pandora::Algorithm *VisualMonitoringAlgorithm::Factory::CreateAlgorithm() const
{
    return new VisualMonitoringAlgorithm();
}

#endif // #ifndef VISUAL_MONITORING_ALGORITHM_H
