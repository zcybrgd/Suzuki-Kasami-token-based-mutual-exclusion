# Ricart-Agrawala-with-token-based-mutual-exclusion
simulating Ricart &amp; Agrawala 1983 with token-based mutual exclusion, peer-to-peer sockets, multithreading, and GUI to visualize process communication and token handover 

## Compilation

make


## Execution de la simulation

make run
ou directement 
./ra_simulation


## Controle de la simulation

La simulation s'exécute indéfiniment jusqu'à ce que vous appuyiez sur Ctrl+C
Les pannes sont simulées aléatoirement et la récupération est automatique
Tous les événements sont journalisés dans la console


## Fonctionnement de l'algorithme R&A83 avec jeton

L'algorithme de Ricart & Agrawala 83 avec jeton qu'on a implémenté fonctionne comme suit :

### Initialisation

Un seul processus (ici le processus 0) possède initialement le jeton

Chaque processus maintient deux tableaux :
jeton : mémorise les estampilles des dernières visites du jeton à chaque processus
requetes : mémorise les estampilles des dernières requêtes de chaque processus


### Demande de Section Critique

Si le processus a déjà le jeton, il entre directement en SC
Sinon, il incrémente son horloge logique, met à jour son tableau de requêtes et diffuse une demande à tous les autres processus


### Traitement des requêtes

À la réception d'une requête, un processus met à jour son horloge logique
Si le processus possède le jeton et n'est pas en SC, il vérifie si le demandeur a besoin du jeton (requete[j] > jeton[j])
Si c'est le cas, il envoie le jeton au demandeur


### Sortie de Section Critique

Le processus met à jour le jeton pour refléter sa visite (jeton[id] = horloge)
Il cherche s'il existe un processus qui attend le jeton
Si oui, il lui envoie le jeton



## Gestion des horloges logiques
L'implémentation utilise des horloges logiques de Lamport :

Chaque processus maintient une horloge locale 
L'horloge est incrémentée à chaque envoi de message
À la réception d'un message, l'horloge est mise à jour selon la règle : horloge = max(horloge_locale, horloge_message) + 1

## Gestion des sockets
La communication par sockets est implémentée de manière bidirectionnelle :

Chaque processus est à la fois client et serveur (modèle peer to peer)
Un thread serveur accepte les connexions entrantes
Un thread par client gère les messages reçus
Des sockets TCP assurent une communication fiable entre les processus

## Simulation des pannes
Les pannes sont simulées de manière aléatoire :

Un thread dédié (simulateFaults) sélectionne périodiquement un processus au hasard
Le processus est mis en état FAILED pendant une durée aléatoire
Pendant cette période, il n'émet pas de requêtes et ne traite pas les messages
Après la période de panne, le processus revient à l'état IDLE


## Tolérance aux pannes robuste : (on doit demander au professeur si c demande de faire ca)

Implémenter un algorithme de détection de panne
Ajouter un mécanisme de régénération du jeton en cas de perte





