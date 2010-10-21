/**
 *  @file   PandoraPFANew/src/Algorithms/MuonReconstructionAlgorithm.cc
 * 
 *  @brief  Implementation of the muon reconstruction algorithm class.
 * 
 *  $Log: $
 */

#include "Algorithms/MuonReconstructionAlgorithm.h"

#include "Pandora/AlgorithmHeaders.h"

using namespace pandora;

StatusCode MuonReconstructionAlgorithm::Run()
{
    // Store names of input track and calo hit lists
    std::string inputTrackListName;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentTrackListName(*this, inputTrackListName));

    std::string inputCaloHitListName;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentOrderedCaloHitListName(*this, inputCaloHitListName));

    // Cluster the muon hits
    std::string muonClusterListName;
    const ClusterList *pMuonClusterList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ReplaceCurrentOrderedCaloHitList(*this, m_muonCaloHitListName));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RunClusteringAlgorithm(*this, m_muonClusteringAlgName, pMuonClusterList,
        muonClusterListName));

    if (pMuonClusterList->empty())
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ReplaceCurrentOrderedCaloHitList(*this, inputCaloHitListName));
        return STATUS_CODE_SUCCESS;
    }

    // Associate muon clusters to tracks
    if (m_shouldCheatTrackAssociation)
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->CheatAssociateMuonTracks(pMuonClusterList));
    }
    else
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->AssociateMuonTracks(pMuonClusterList));
    }

    // Add ecal/hcal hits to the muon cluster
    if (m_shouldCheatCaloHitAddition)
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->CheatAddCaloHits(pMuonClusterList, inputCaloHitListName));
    }
    else
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->AddCaloHits(pMuonClusterList, inputCaloHitListName));
    }

    // Complete the reconstruction
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->CreateMuonPfos(pMuonClusterList));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->TidyLists(inputTrackListName, inputCaloHitListName, muonClusterListName));

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::AssociateMuonTracks(const ClusterList *const pMuonClusterList) const
{
    static const float muonEndCapInnerZ(GeometryHelper::GetInstance()->GetMuonEndCapParameters().GetInnerZCoordinate());
    static const float coilMidPointR(0.5f * (GeometryHelper::GetInstance()->GetCoilOuterRadius() + GeometryHelper::GetInstance()->GetCoilInnerRadius()));

    const TrackList *pTrackList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentTrackList(*this, pTrackList));

    for (ClusterList::const_iterator iter = pMuonClusterList->begin(), iterEnd = pMuonClusterList->end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;

        // Simple cuts on cluster properties
        if (pCluster->GetNCaloHits() > m_maxClusterCaloHits)
            continue;

        if (pCluster->GetOrderedCaloHitList().size() < m_minClusterOccupiedLayers)
            continue;

        if ((pCluster->GetOuterPseudoLayer() - pCluster->GetInnerPseudoLayer()) < m_minClusterLayerSpan)
            continue;

        // Get direction of the cluster
        ClusterHelper::ClusterFitResult clusterFitResult;
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, ClusterHelper::FitStart(pCluster, m_nClusterLayersToFit, clusterFitResult));

        if (!clusterFitResult.IsFitSuccessful())
            continue;

        const CartesianVector clusterInnerCentroid(pCluster->GetCentroid(pCluster->GetInnerPseudoLayer()));

        // Loop over all non-associated tracks in the current track list to find bestTrack
        Track *pBestTrack(NULL);
        float bestTrackEnergy(0.f);
        float bestDistanceToTrack(m_maxDistanceToTrack);

        for (TrackList::const_iterator iterT = pTrackList->begin(), iterTEnd = pTrackList->end(); iterT != iterTEnd; ++iterT)
        {
            Track *pTrack = *iterT;

            // Simple cuts on track properties
            if (pTrack->HasAssociatedCluster() || !pTrack->CanFormPfo())
                continue;

            if (!pTrack->GetDaughterTrackList().empty())
                continue;

            if (pTrack->GetEnergyAtDca() < m_minTrackCandidateEnergy)
                continue;

            // Extract track helix fit
            const Helix *const pHelix(pTrack->GetHelixFitAtECal());

            // Compare cluster and helix directions
            CartesianVector muonEntryPosition;
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pHelix->GetPointInZ((clusterInnerCentroid.GetZ() < 0.f) ? -muonEndCapInnerZ : muonEndCapInnerZ, pHelix->GetReferencePoint(), muonEntryPosition));

            const float muonEntryR(std::sqrt(muonEntryPosition.GetX() * muonEntryPosition.GetX() + muonEntryPosition.GetY() * muonEntryPosition.GetY()));
            bool isInBarrel(false);

            if (muonEntryR > coilMidPointR)
            {
                isInBarrel = true;
                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pHelix->GetPointOnCircle(coilMidPointR, pHelix->GetReferencePoint(), muonEntryPosition));
            }

            const CartesianVector muonEntryMomentum(pHelix->GetExtrapolatedMomentum(muonEntryPosition));
            const CartesianVector helixDirection(muonEntryPosition.GetUnitVector());
            const Helix externalHelix(muonEntryPosition, muonEntryMomentum, isInBarrel ? -pHelix->GetCharge() : pHelix->GetCharge(), isInBarrel ? 1.5f : 4.f); // TODO get bfield outside

            const float helixClusterCosAngle(helixDirection.GetCosOpeningAngle(clusterFitResult.GetDirection()));

            if (helixClusterCosAngle < m_minHelixClusterCosAngle)
                continue;

            // Calculate separation of helix and cluster inner centroid
            CartesianVector helixSeparation;
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, externalHelix.GetDistanceToPoint(clusterInnerCentroid, helixSeparation));

            const float distanceToTrack(helixSeparation.GetZ());

            if ((distanceToTrack < bestDistanceToTrack) || ((distanceToTrack == bestDistanceToTrack) && (pTrack->GetEnergyAtDca() > bestTrackEnergy)))
            {
                pBestTrack = pTrack;
                bestDistanceToTrack = distanceToTrack;
                bestTrackEnergy = pTrack->GetEnergyAtDca();
            }
        }

        if (NULL != pBestTrack)
        {
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddTrackClusterAssociation(*this, pBestTrack, pCluster));
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::AddCaloHits(const ClusterList *const pMuonClusterList, const std::string &inputCaloHitListName) const
{
    static const float hCalEndCapInnerR(GeometryHelper::GetInstance()->GetHCalEndCapParameters().GetInnerRCoordinate());
    static const float eCalEndCapInnerR(GeometryHelper::GetInstance()->GetECalEndCapParameters().GetInnerRCoordinate());

    const OrderedCaloHitList *pOrderedCaloHitList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetOrderedCaloHitList(*this, inputCaloHitListName, pOrderedCaloHitList));

    for (ClusterList::const_iterator clusterIter = pMuonClusterList->begin(), clusterIterEnd = pMuonClusterList->end(); clusterIter != clusterIterEnd; ++clusterIter)
    {
        Cluster *pCluster = *clusterIter;

        // Check track associations
        const TrackList &trackList(pCluster->GetAssociatedTrackList());

        if (trackList.size() != m_nExpectedTracksPerCluster)
            continue;

        Track *pTrack = *(trackList.begin());
        const Helix *const pHelix(pTrack->GetHelixFitAtECal());

        for (OrderedCaloHitList::const_iterator layerIter = pOrderedCaloHitList->begin(), layerIterEnd = pOrderedCaloHitList->end(); layerIter != layerIterEnd; ++layerIter)
        {
            TrackDistanceInfoVector trackDistanceInfoVector;
            unsigned int nHitsInRegion1(0), nHitsInRegion2(0);

            for (CaloHitList::const_iterator hitIter = layerIter->second->begin(), hitIterEnd = layerIter->second->end(); hitIter != hitIterEnd; ++hitIter)
            {
                CaloHit *pCaloHit = *hitIter;

                if (!CaloHitHelper::IsCaloHitAvailable(pCaloHit) || (!m_shouldClusterIsolatedHits && pCaloHit->IsIsolated()))
                    continue;

                const CartesianVector &caloHitPosition(pCaloHit->GetPositionVector());
                const CartesianVector helixDirection(pHelix->GetExtrapolatedMomentum(caloHitPosition).GetUnitVector());

                if (caloHitPosition.GetCosOpeningAngle(helixDirection) < m_minHelixCaloHitCosAngle)
                    continue;

                if (ENDCAP == pCaloHit->GetDetectorRegion())
                {
                    CartesianVector intersectionPoint;
                    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pHelix->GetPointInZ(caloHitPosition.GetZ(), pHelix->GetReferencePoint(), intersectionPoint));

                    const float helixR(std::sqrt(intersectionPoint.GetX() * intersectionPoint.GetX() + intersectionPoint.GetY() * intersectionPoint.GetY()));

                    if ((HCAL == pCaloHit->GetHitType()) && (helixR < hCalEndCapInnerR))
                        continue;

                    if ((ECAL == pCaloHit->GetHitType()) && (helixR < eCalEndCapInnerR))
                        continue;
                }

                CartesianVector helixSeparation;
                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pHelix->GetDistanceToPoint(caloHitPosition, helixSeparation));

                const float cellLengthScale(pCaloHit->GetCellLengthScale());

                if (0.f == cellLengthScale)
                    continue;

                const float genericDistance(helixSeparation.GetMagnitude() / cellLengthScale);
                trackDistanceInfoVector.push_back(TrackDistanceInfo(pCaloHit, genericDistance));

                if (genericDistance < m_region1GenericDistance)
                {
                    ++nHitsInRegion1;
                }
                else if (genericDistance < m_region2GenericDistance)
                {
                    ++nHitsInRegion2;
                }
            }

            const bool isIsolated((nHitsInRegion1 >= m_isolatedMinRegion1Hits) && (nHitsInRegion2 <= m_isolatedMaxRegion2Hits));
            std::sort(trackDistanceInfoVector.begin(), trackDistanceInfoVector.end(), MuonReconstructionAlgorithm::SortByDistanceToTrack);

            for (TrackDistanceInfoVector::const_iterator iter = trackDistanceInfoVector.begin(), iterEnd = trackDistanceInfoVector.end(); iter != iterEnd; ++iter)
            {
                if ((iter->second > m_maxGenericDistance) || (isIsolated && (iter->second > m_isolatedMaxGenericDistance)))
                    break;

                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddCaloHitToCluster(*this, pCluster, iter->first));

                if (!isIsolated)
                    break;
            }
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::CheatAssociateMuonTracks(const ClusterList *const pMuonClusterList) const
{
    const TrackList *pTrackList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentTrackList(*this, pTrackList));

    for (ClusterList::const_iterator iter = pMuonClusterList->begin(), iterEnd = pMuonClusterList->end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;

        const MCParticle *pBestMCParticle = NULL;
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->GetBestMCParticle(pCluster, pBestMCParticle));

        const Uid bestUid(pBestMCParticle->GetUid());

        for (TrackList::const_iterator trackIter = pTrackList->begin(), trackIterEnd = pTrackList->end(); trackIter != trackIterEnd; ++trackIter)
        {
            Track *pTrack = *trackIter;

            // Simple cuts on track properties
            if (pTrack->HasAssociatedCluster() || !pTrack->CanFormPfo())
                continue;

            if (!pTrack->GetDaughterTrackList().empty())
                continue;

            if (pTrack->GetEnergyAtDca() < m_minTrackCandidateEnergy)
                continue;

            const MCParticle *pMCTrackParticle = NULL;

            if (STATUS_CODE_SUCCESS != pTrack->GetMCParticle(pMCTrackParticle))
                continue;

            if (this->IsMatchedMCParticle(pMCTrackParticle, bestUid))
            {
                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddTrackClusterAssociation(*this, pTrack, pCluster));
                break;
            }
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::CheatAddCaloHits(const ClusterList *const pMuonClusterList, const std::string &inputCaloHitListName) const
{
    const OrderedCaloHitList *pOrderedCaloHitList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetOrderedCaloHitList(*this, inputCaloHitListName, pOrderedCaloHitList));

    for (ClusterList::const_iterator clusterIter = pMuonClusterList->begin(), clusterIterEnd = pMuonClusterList->end(); clusterIter != clusterIterEnd; ++clusterIter)
    {
        Cluster *pCluster = *clusterIter;

        const MCParticle *pBestMCParticle = NULL;
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->GetBestMCParticle(pCluster, pBestMCParticle));

        // Check track associations
        const TrackList &trackList(pCluster->GetAssociatedTrackList());

        if (trackList.size() != m_nExpectedTracksPerCluster)
            continue;

        for (OrderedCaloHitList::const_iterator layerIter = pOrderedCaloHitList->begin(), layerIterEnd = pOrderedCaloHitList->end(); layerIter != layerIterEnd; ++layerIter)
        {
            for (CaloHitList::const_iterator hitIter = layerIter->second->begin(), hitIterEnd = layerIter->second->end(); hitIter != hitIterEnd; ++hitIter)
            {
                CaloHit *pCaloHit = *hitIter;

                if (!CaloHitHelper::IsCaloHitAvailable(pCaloHit) || (!m_shouldClusterIsolatedHits && pCaloHit->IsIsolated()))
                    continue;

                const MCParticle *pMCParticle = NULL;

                if (STATUS_CODE_SUCCESS != pCaloHit->GetMCParticle(pMCParticle))
                    continue;

                if (pBestMCParticle->GetUid() != pMCParticle->GetUid())
                    continue;

                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddCaloHitToCluster(*this, pCluster, pCaloHit));
            }
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::CreateMuonPfos(const ClusterList *const pMuonClusterList) const
{
    for (ClusterList::const_iterator iter = pMuonClusterList->begin(), iterEnd = pMuonClusterList->end(); iter != iterEnd; ++iter)
    {
        PandoraContentApi::ParticleFlowObject::Parameters pfoParameters;

        Cluster *pCluster = *iter;
        pfoParameters.m_clusterList.insert(pCluster);

        // Consider associated tracks
        const TrackList &trackList(pCluster->GetAssociatedTrackList());

        if (trackList.size() != m_nExpectedTracksPerCluster)
            continue;

        Track *pTrack = *(trackList.begin());
        pfoParameters.m_trackList.insert(pTrack);

        // Examine track relationships
        const TrackList &parentTrackList(pTrack->GetParentTrackList());

        if ((parentTrackList.size() > m_nExpectedParentTracks) || !pTrack->GetDaughterTrackList().empty() || !pTrack->GetSiblingTrackList().empty())
        {
            std::cout << "MuonReconstructionAlgorithm: invalid/unexpected track relationships for muon." << std::endl;
            continue;
        }

        if (!parentTrackList.empty())
        {
            pfoParameters.m_trackList.insert(parentTrackList.begin(), parentTrackList.end());
        }

        pfoParameters.m_energy = pTrack->GetEnergyAtDca();
        pfoParameters.m_momentum = pTrack->GetMomentumAtDca();
        pfoParameters.m_mass = pTrack->GetMass();
        pfoParameters.m_charge = pTrack->GetCharge();
        pfoParameters.m_particleId = (pTrack->GetCharge() > 0) ? MU_PLUS : MU_MINUS;

        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ParticleFlowObject::Create(*this, pfoParameters));
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::TidyLists(const std::string &inputTrackListName, const std::string &inputCaloHitListName,
    const std::string &muonClusterListName) const
{
    // Make list of all tracks, clusters and calo hits in muon pfos
    TrackList pfoTrackList; OrderedCaloHitList pfoCaloHitList; ClusterList pfoClusterList;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->GetPfoComponents(pfoTrackList, pfoCaloHitList, pfoClusterList));

    // Save the muon-removed track list
    const TrackList *pInputTrackList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetTrackList(*this, inputTrackListName, pInputTrackList));

    TrackList outputTrackList(*pInputTrackList);

    if (!pfoTrackList.empty())
    {
        for (TrackList::const_iterator iter = pfoTrackList.begin(), iterEnd = pfoTrackList.end(); iter != iterEnd; ++iter)
            outputTrackList.erase(*iter);
    }

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::SaveTrackListAndReplaceCurrent(*this, outputTrackList, m_outputTrackListName));

    // Save the muon-removed calo hit list
    const OrderedCaloHitList *pInputCaloHitList = NULL;
    const OrderedCaloHitList *pMuonCaloHitList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetOrderedCaloHitList(*this, inputCaloHitListName, pInputCaloHitList));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetOrderedCaloHitList(*this, m_muonCaloHitListName, pMuonCaloHitList));

    OrderedCaloHitList outputCaloHitList(*pInputCaloHitList);
    OrderedCaloHitList outputMuonCaloHitList(*pMuonCaloHitList);

    if (!pfoCaloHitList.empty())
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, outputCaloHitList.Remove(pfoCaloHitList));
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, outputMuonCaloHitList.Remove(pfoCaloHitList));
    }

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::SaveOrderedCaloHitList(*this, outputMuonCaloHitList, m_outputMuonCaloHitListName));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::SaveOrderedCaloHitListAndReplaceCurrent(*this, outputCaloHitList, m_outputCaloHitListName));

    // Save the muon cluster list
    if (!pfoClusterList.empty())
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::SaveClusterList(*this, muonClusterListName, m_outputMuonClusterListName, pfoClusterList));
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::GetBestMCParticle(const Cluster *const pCluster, const MCParticle *&pBestMCParticle) const
{
    bool isFound = false;

    typedef std::map<const MCParticle *, float> MCParticleToEnergyMap;
    MCParticleToEnergyMap mcParticleToEnergyMap;

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());

    for (OrderedCaloHitList::const_iterator layerIter = orderedCaloHitList.begin(), layerIterEnd = orderedCaloHitList.end(); layerIter != layerIterEnd; ++layerIter)
    {
        for (CaloHitList::const_iterator hitIter = layerIter->second->begin(), hitIterEnd = layerIter->second->end(); hitIter != hitIterEnd; ++hitIter)
        {
            CaloHit *pCaloHit = *hitIter;
            const MCParticle *pMCParticle = NULL;

            if (STATUS_CODE_SUCCESS != pCaloHit->GetMCParticle(pMCParticle))
                continue;

            mcParticleToEnergyMap[pMCParticle] += pCaloHit->GetHadronicEnergy();
        }
    }

    float bestEnergy(0.f);

    for (MCParticleToEnergyMap::const_iterator iter = mcParticleToEnergyMap.begin(), iterEnd = mcParticleToEnergyMap.end(); iter != iterEnd; ++iter)
    {
        const float energy(iter->second);

        if (energy > bestEnergy)
        {
            bestEnergy = energy;
            pBestMCParticle = iter->first;
            isFound = true;
        }
    }

    if (isFound)
    {
        return STATUS_CODE_SUCCESS;
    }

    return STATUS_CODE_NOT_FOUND;
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool MuonReconstructionAlgorithm::IsMatchedMCParticle(const MCParticle *const pMCParticle, const Uid uid) const
{
    if (uid == pMCParticle->GetUid())
        return true;

    const MCParticleList &daughterList(pMCParticle->GetDaughterList());

    for (MCParticleList::const_iterator iter = daughterList.begin(), iterEnd = daughterList.end(); iter != iterEnd; ++iter)
    {
        if (uid == (*iter)->GetUid())
            return true;

        if (this->IsMatchedMCParticle(*iter, uid))
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::GetPfoComponents(TrackList &pfoTrackList, OrderedCaloHitList &pfoCaloHitList, ClusterList &pfoClusterList) const
{
    pfoTrackList.clear(); pfoCaloHitList.clear(); pfoClusterList.clear();

    const ParticleFlowObjectList *pPfoList = NULL;
    const StatusCode statusCode(PandoraContentApi::GetCurrentPfoList(*this, pPfoList));

    if (STATUS_CODE_NOT_INITIALIZED == statusCode)
        return STATUS_CODE_SUCCESS;

    if (STATUS_CODE_SUCCESS != statusCode)
        return statusCode;

    for (ParticleFlowObjectList::const_iterator iter = pPfoList->begin(), iterEnd = pPfoList->end(); iter != iterEnd; ++iter)
    {
        ParticleFlowObject *pPfo = *iter;
        pfoTrackList.insert(pPfo->GetTrackList().begin(), pPfo->GetTrackList().end());
        pfoClusterList.insert(pPfo->GetClusterList().begin(), pPfo->GetClusterList().end());
    }

    for (ClusterList::const_iterator iter = pfoClusterList.begin(), iterEnd = pfoClusterList.end(); iter != iterEnd; ++iter)
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pfoCaloHitList.Add((*iter)->GetOrderedCaloHitList()));
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, pfoCaloHitList.Add((*iter)->GetIsolatedCaloHitList()));
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode MuonReconstructionAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    // Input lists
    m_muonCaloHitListName = "Muon";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "m_muonCaloHitListName", m_muonCaloHitListName));

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithm(*this, xmlHandle,
        "MuonClusterFormation", m_muonClusteringAlgName));

    // Algorithm steering
    m_shouldCheatTrackAssociation = false;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ShouldCheatTrackAssociation", m_shouldCheatTrackAssociation));

    m_shouldCheatCaloHitAddition = false;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ShouldCheatCaloHitAddition", m_shouldCheatCaloHitAddition));

    // Cluster-track association
    m_maxClusterCaloHits = 30;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxClusterCaloHits", m_maxClusterCaloHits));

    m_minClusterOccupiedLayers = 8;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinClusterOccupiedLayers", m_minClusterOccupiedLayers));

    m_minClusterLayerSpan = 8;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinClusterLayerSpan", m_minClusterLayerSpan));

    m_nClusterLayersToFit = 100;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NClusterLayersToFit", m_nClusterLayersToFit));

    m_maxClusterFitChi2 = 4.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxClusterFitChi2", m_maxClusterFitChi2));

    m_maxDistanceToTrack = 1500.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxDistanceToTrack", m_maxDistanceToTrack));

    m_minTrackCandidateEnergy = 4.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinTrackCandidateEnergy", m_minTrackCandidateEnergy));

    m_minHelixClusterCosAngle = 0.95f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinHelixClusterCosAngle", m_minHelixClusterCosAngle));

    // Addition of ecal/hcal hits
    m_nExpectedTracksPerCluster = 1;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NExpectedTracksPerCluster", m_nExpectedTracksPerCluster));

    if (0 == m_nExpectedTracksPerCluster)
        return STATUS_CODE_INVALID_PARAMETER;

    m_nExpectedParentTracks = 1;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "NExpectedParentTracks", m_nExpectedParentTracks));

    m_minHelixCaloHitCosAngle = 0.95f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinHelixCaloHitCosAngle", m_minHelixCaloHitCosAngle));

    m_region1GenericDistance = 3.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "Region1GenericDistance", m_region1GenericDistance));

    m_region2GenericDistance = 6.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "Region2GenericDistance", m_region2GenericDistance));

    m_isolatedMinRegion1Hits = 1;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "IsolatedMinRegion1Hits", m_isolatedMinRegion1Hits));

    m_isolatedMaxRegion2Hits = 0;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "IsolatedMaxRegion2Hits", m_isolatedMaxRegion2Hits));

    m_maxGenericDistance = 6.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxGenericDistance", m_maxGenericDistance));

    m_isolatedMaxGenericDistance = 3.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "IsolatedMaxGenericDistance", m_isolatedMaxGenericDistance));

    // Output lists
    m_outputMuonClusterListName = "MuonClusters";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "OutputMuonClusterListName", m_outputMuonClusterListName));

    m_outputTrackListName = "MuonRemovedTracks";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "OutputTrackListName", m_outputTrackListName));

    m_outputCaloHitListName = "MuonRemovedCaloHits";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "OutputCaloHitListName", m_outputCaloHitListName));

    m_outputMuonCaloHitListName = "MuonRemovedYokeHits";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "OutputMuonCaloHitListName", m_outputMuonCaloHitListName));

    m_shouldClusterIsolatedHits = false;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ShouldClusterIsolatedHits", m_shouldClusterIsolatedHits));

    return STATUS_CODE_SUCCESS;
}
