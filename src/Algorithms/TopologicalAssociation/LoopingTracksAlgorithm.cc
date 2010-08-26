/**
 *  @file   PandoraPFANew/src/Algorithms/TopologicalAssociation/LoopingTracksAlgorithm.cc
 * 
 *  @brief  Implementation of the looping tracks algorithm class.
 * 
 *  $Log: $
 */

#include "Algorithms/TopologicalAssociation/LoopingTracksAlgorithm.h"

#include "Pandora/AlgorithmHeaders.h"

using namespace pandora;

StatusCode LoopingTracksAlgorithm::Run()
{
    const ClusterList *pClusterList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentClusterList(*this, pClusterList));

    ClusterVector clusterVector(pClusterList->begin(), pClusterList->end());
    std::sort(clusterVector.begin(), clusterVector.end(), Cluster::SortByInnerLayer);

    static const unsigned int nECalLayers(GeometryHelper::GetInstance()->GetECalBarrelParameters().GetNLayers());

    // Fit a straight line to the last n occupied pseudo layers in each cluster and store results
    ClusterFitRelationList clusterFitRelationList;

    for (ClusterVector::const_iterator iter = clusterVector.begin(), iterEnd = clusterVector.end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;

        if (!ClusterHelper::CanMergeCluster(pCluster, m_canMergeMinMipFraction, m_canMergeMaxRms))
            continue;

        if ((pCluster->GetNCaloHits() < m_minHitsInCluster) || (pCluster->GetOrderedCaloHitList().size() < m_minOccupiedLayersInCluster))
            continue;

        ClusterHelper::ClusterFitResult clusterFitResult;
        (void) ClusterHelper::FitEnd(pCluster, m_nLayersToFit, clusterFitResult);

        if (clusterFitResult.IsFitSuccessful() && (clusterFitResult.GetChi2() < m_fitChi2Cut))
            clusterFitRelationList.push_back(new ClusterFitRelation(pCluster, clusterFitResult));
    }

    // Loop over cluster combinations, comparing fit results to determine whether clusters should be merged
    for (ClusterFitRelationList::const_iterator iterI = clusterFitRelationList.begin(), iterIEnd = clusterFitRelationList.end(); iterI != iterIEnd; ++iterI)
    {
        if ((*iterI)->IsDefunct())
            continue;

        Cluster *pParentCluster((*iterI)->GetCluster());
        const ClusterHelper::ClusterFitResult &parentClusterFitResult((*iterI)->GetClusterFitResult());

        const PseudoLayer parentOuterLayer(pParentCluster->GetOuterPseudoLayer());
        const bool isParentOutsideECal(parentOuterLayer > nECalLayers);
        const bool isParentDeepInHCal(parentOuterLayer > nECalLayers + m_nDeepInHCalLayers);

        ClusterFitRelation *pBestClusterFitRelation(NULL);
        float minFitResultsApproach(std::numeric_limits<float>::max());
        ClusterFitRelationList::const_iterator iterJ(iterI);

        for (++iterJ; iterJ != clusterFitRelationList.end(); ++iterJ)
        {
            // Check to see if cluster has already been changed
            if ((*iterJ)->IsDefunct())
                continue;

            Cluster *pDaughterCluster((*iterJ)->GetCluster());
            const ClusterHelper::ClusterFitResult &daughterClusterFitResult((*iterJ)->GetClusterFitResult());

            // Are both clusters outside of the ecal region? If so, relax cluster compatibility checks.
            const PseudoLayer daughterOuterLayer(pDaughterCluster->GetOuterPseudoLayer());

            const bool isDaughterOutsideECal(daughterOuterLayer > nECalLayers);
            const bool isDaughterDeepInHCal(daughterOuterLayer > nECalLayers + m_nDeepInHCalLayers);
            const bool isOutsideECal(isParentOutsideECal && isDaughterOutsideECal);
            const bool isDeepInHCal(isParentDeepInHCal && isDaughterDeepInHCal);

            // Apply loose cuts to examine suitability of merging clusters before proceeding
            const PseudoLayer outerLayerDifference((parentOuterLayer > daughterOuterLayer) ? (parentOuterLayer - daughterOuterLayer) :
                (daughterOuterLayer - parentOuterLayer));

            if (outerLayerDifference > m_maxOuterLayerDifference)
                continue;

            const CartesianVector centroidDifference(pParentCluster->GetCentroid(parentOuterLayer) - pDaughterCluster->GetCentroid(daughterOuterLayer));

            if (centroidDifference.GetMagnitude() > m_maxCentroidDifference)
                continue;

            // Check that cluster fit directions are compatible with looping track hypothesis
            const float fitDirectionDotProductCut(isDeepInHCal ? m_fitDirectionDotProductCutHCal : m_fitDirectionDotProductCutECal);
            const float fitDirectionDotProduct(parentClusterFitResult.GetDirection().GetDotProduct(daughterClusterFitResult.GetDirection()));

            if (fitDirectionDotProduct > fitDirectionDotProductCut)
                continue;

            if (centroidDifference.GetDotProduct(daughterClusterFitResult.GetDirection() - parentClusterFitResult.GetDirection()) <= 0.)
                continue;

            // Cut on distance of closest approach between hits in outer layers of the two clusters
            const float closestHitDistance(this->GetClosestDistanceBetweenOuterLayerHits(pParentCluster, pDaughterCluster));
            const float closestHitDistanceCut(isDeepInHCal ? m_closestHitDistanceCutHCal : m_closestHitDistanceCutECal);

            if (closestHitDistance > closestHitDistanceCut)
                continue;

            // Cut on distance of closest approach between fit extrapolations
            const float fitResultsClosestApproachCut(isOutsideECal ? m_fitResultsClosestApproachCutHCal : m_fitResultsClosestApproachCutECal);
            float fitResultsClosestApproach(std::numeric_limits<float>::max());

            if (STATUS_CODE_SUCCESS != ClusterHelper::GetFitResultsClosestApproach(parentClusterFitResult, daughterClusterFitResult, fitResultsClosestApproach))
                continue;

            if ((fitResultsClosestApproach > fitResultsClosestApproachCut) || (fitResultsClosestApproach > minFitResultsApproach))
                continue;

            // Merge clusters if they are in HCal, otherwise look for "good" features (bit ad hoc) ...
            unsigned int nGoodFeatures(0);

            if (!isDeepInHCal)
            {
                if (fitDirectionDotProduct < m_goodFeaturesMaxFitDotProduct)
                    nGoodFeatures++;

                if (fitResultsClosestApproach < m_goodFeaturesMaxFitApproach)
                    nGoodFeatures++;

                if (outerLayerDifference < m_goodFeaturesMaxLayerDifference)
                    nGoodFeatures++;

                if ((pParentCluster->GetMipFraction() > m_goodFeaturesMinMipFraction) && (pDaughterCluster->GetMipFraction() > m_goodFeaturesMinMipFraction))
                    nGoodFeatures++;
            }

            // Now have sufficient information to decide whether to join clusters
            if (isDeepInHCal || (nGoodFeatures >= m_nGoodFeaturesForClusterMerge))
            {
                pBestClusterFitRelation = *iterJ;
                minFitResultsApproach = fitResultsClosestApproach;
            }
        }

        if (NULL != pBestClusterFitRelation)
        {
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::MergeAndDeleteClusters(*this, pParentCluster, pBestClusterFitRelation->GetCluster()));
            pBestClusterFitRelation->SetAsDefunct();
            --iterI;
        }
    }

    // Tidy up
    for (ClusterFitRelationList::const_iterator iter = clusterFitRelationList.begin(), iterEnd = clusterFitRelationList.end(); iter != iterEnd; ++iter)
        delete (*iter);

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

float LoopingTracksAlgorithm::GetClosestDistanceBetweenOuterLayerHits(const Cluster *const pClusterI, const Cluster *const pClusterJ) const
{
    float closestDistance(std::numeric_limits<float>::max());

    const PseudoLayer outerLayerI(pClusterI->GetOuterPseudoLayer());
    const PseudoLayer outerLayerJ(pClusterJ->GetOuterPseudoLayer());

    CaloHitList *pCaloHitListI = NULL;
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, pClusterI->GetOrderedCaloHitList().GetCaloHitsInPseudoLayer(outerLayerI, pCaloHitListI));

    CaloHitList *pCaloHitListJ = NULL;
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, pClusterJ->GetOrderedCaloHitList().GetCaloHitsInPseudoLayer(outerLayerJ, pCaloHitListJ));

    for (CaloHitList::const_iterator iterI = pCaloHitListI->begin(), iterIEnd = pCaloHitListI->end(); iterI != iterIEnd; ++iterI)
    {
        CaloHit *pCaloHitI = *iterI;

        for (CaloHitList::const_iterator iterJ = pCaloHitListJ->begin(), iterIEnd = pCaloHitListJ->end(); iterJ != iterIEnd; ++iterJ)
        {
            CaloHit *pCaloHitJ = *iterJ;

            const float distance((pCaloHitI->GetPositionVector() - pCaloHitJ->GetPositionVector()).GetMagnitude());

            if (distance < closestDistance)
                closestDistance = distance;
        }
    }

    return closestDistance;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode LoopingTracksAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    m_nLayersToFit = 5;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NLayersToFit", m_nLayersToFit));

    m_fitChi2Cut = 100.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FitChi2Cut", m_fitChi2Cut));

    m_nDeepInHCalLayers = 10;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NDeepInHCalLayers", m_nDeepInHCalLayers));

    m_canMergeMinMipFraction = 0.7f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "CanMergeMinMipFraction", m_canMergeMinMipFraction));

    m_canMergeMaxRms = 5.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "CanMergeMaxRms", m_canMergeMaxRms));

    m_minHitsInCluster = 4;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinHitsInCluster", m_minHitsInCluster));

    m_minOccupiedLayersInCluster = 2;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinOccupiedLayersInCluster", m_minOccupiedLayersInCluster));

    m_maxOuterLayerDifference = 6;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxOuterLayerDifference", m_maxOuterLayerDifference));

    m_maxCentroidDifference = 2000.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxCentroidDifference", m_maxCentroidDifference));

    m_fitDirectionDotProductCutECal = -0.1f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FitDirectionDotProductCutECal", m_fitDirectionDotProductCutECal));

    m_fitDirectionDotProductCutHCal = 0.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FitDirectionDotProductCutHCal", m_fitDirectionDotProductCutHCal));

    m_closestHitDistanceCutECal = 250.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ClosestHitDistanceCutECal", m_closestHitDistanceCutECal));

    m_closestHitDistanceCutHCal = 500.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ClosestHitDistanceCutHCal", m_closestHitDistanceCutHCal));

    m_fitResultsClosestApproachCutECal = 50.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FitResultsClosestApproachCutECal", m_fitResultsClosestApproachCutECal));

    m_fitResultsClosestApproachCutHCal = 200.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FitResultsClosestApproachCutHCal", m_fitResultsClosestApproachCutHCal));

    m_nGoodFeaturesForClusterMerge = 2;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NGoodFeaturesForClusterMerge", m_nGoodFeaturesForClusterMerge));

    m_goodFeaturesMaxFitDotProduct = -0.5f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NGoodFeaturesForClusterMerge", m_nGoodFeaturesForClusterMerge));

    m_goodFeaturesMaxFitApproach = 50.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "GoodFeaturesMaxFitApproach", m_goodFeaturesMaxFitApproach));

    m_goodFeaturesMaxLayerDifference = 4;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "GoodFeaturesMaxLayerDifference", m_goodFeaturesMaxLayerDifference));

    m_goodFeaturesMinMipFraction = 0.9f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "GoodFeaturesMinMipFraction", m_goodFeaturesMinMipFraction));

    return STATUS_CODE_SUCCESS;
}
